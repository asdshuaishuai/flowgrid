#include "peer_session.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QHostAddress>

namespace fg::input {

namespace {

// 帧类型（§5.1）：会话握手用的两个，值与 protocol/frame.go 一致。
constexpr std::uint8_t kFrameIdentify = 0x10;
constexpr std::uint8_t kFrameIdentifyRes = 0x11;

// IDENTIFY_RESPONSE 的 status（§4.2）：0 = 接受。
constexpr std::uint8_t kIdentifyAccepted = 0x00;
constexpr std::uint8_t kIdentifyRejected = 0x01;

// 帧时间戳单位是微秒（protocol/frame.go：Timestamp = microseconds since epoch）。
constexpr quint64 kTimestampUnit = 1000; // 毫秒 → 微秒

// 长度前缀（§7.2）：2 字节大端，覆盖帧体与 HMAC 尾。
constexpr int kLengthPrefixBytes = 2;
// 单条记录的上限：帧头 12 + 最大载荷 255 + HMAC 8。
constexpr int kMaxRecordBytes = 12 + 255 + 8;

QByteArray deriveSessionKey(const QString &code)
{
    // 与 mac 的 SessionKey.derive 同一算法：SHA256("FlowGrid-mvp-session|" + 归一化码)。
    // 归一化 = 去首尾空白 + 转小写；两端算法不同就解不出对方的帧。
    const QString normalized = code.trimmed().toLower();
    const QByteArray material =
        QByteArrayLiteral("FlowGrid-mvp-session|") + normalized.toUtf8();
    return QCryptographicHash::hash(material, QCryptographicHash::Sha256);
}

} // namespace

PeerSession::PeerSession(QObject *parent) : QObject(parent)
{
    connect(&server_, &QTcpServer::newConnection, this, &PeerSession::onNewConnection);
    // 没配对码也要有一把确定的密钥：两端都空时会派生出同一把，
    // 比「一把没有」更容易排查（帧能通，问题在别处）。
    hmacKey_ = deriveSessionKey(QString());
}

PeerSession::~PeerSession()
{
    close();
}

void PeerSession::setLocalIdentity(const QString &name, std::uint8_t platform)
{
    localName_ = name;
    localPlatform_ = platform;
}

void PeerSession::setPairingCode(const QString &code)
{
    hmacKey_ = deriveSessionKey(code);
}

bool PeerSession::listen(quint16 port)
{
    if (!server_.listen(QHostAddress::AnyIPv4, port)) {
        lastError_ = QStringLiteral("监听 TCP %1 失败：%2")
                         .arg(port)
                         .arg(server_.errorString());
        setState(State::Failed, lastError_);
        return false;
    }
    return true;
}

void PeerSession::connectTo(const QString &host, quint16 port)
{
    resetConnection(QString());

    socket_ = new QTcpSocket(this);
    connect(socket_, &QTcpSocket::connected, this, &PeerSession::onSocketConnected);
    connect(socket_, &QTcpSocket::disconnected, this, &PeerSession::onSocketDisconnected);
    connect(socket_, &QTcpSocket::errorOccurred, this, &PeerSession::onSocketError);
    connect(socket_, &QTcpSocket::readyRead, this, &PeerSession::onSocketReadyRead);

    peerAddress_ = QStringLiteral("%1:%2").arg(host).arg(port);
    setState(State::Connecting, QStringLiteral("正在连接 %1").arg(peerAddress_));
    socket_->connectToHost(host, port);
}

void PeerSession::close()
{
    server_.close();
    dropSocket();
    buffer_.clear();
    setState(State::Idle, QString());
}

void PeerSession::respondInvite(bool accept)
{
    if (socket_ == nullptr || state_ != State::AwaitingAccept) {
        lastError_ = QStringLiteral("当前没有待处理的连接请求");
        return;
    }
    sendIdentifyResponse(accept ? kIdentifyAccepted : kIdentifyRejected);
    responseSent_ = true;
    if (accept) {
        setState(State::Connected, QStringLiteral("已接受 %1 的连接").arg(peerAddress_));
        return;
    }
    setState(State::Rejected, QStringLiteral("已拒绝 %1 的连接").arg(peerAddress_));
    // 回应已经进了发送缓冲区：这里**不能** abort —— abort 会丢弃未发出的
    // 数据，对端根本收不到「拒绝」这个回应，只会看到连接被掐断。
    // disconnectFromHost 会先把缓冲区冲出去再优雅断开。
    if (socket_ != nullptr) {
        socket_->flush();
        socket_->disconnectFromHost();
    }
}

bool PeerSession::sendFrame(std::uint8_t frameType, const QByteArray &payload)
{
    if (socket_ == nullptr || state_ != State::Connected) {
        lastError_ = QStringLiteral("会话未就绪，不能发帧");
        return false;
    }
    const quint64 timestamp =
        static_cast<quint64>(QDateTime::currentMSecsSinceEpoch()) * kTimestampUnit;
    const proto::GoBuffer wire = proto::encodeFrameWithHmac(
        frameType, outSequence_++, timestamp, payload, hmacKey_);
    if (wire.isNull()) {
        lastError_ = QStringLiteral("帧编码失败");
        return false;
    }
    writeRecord(wire.toByteArray());
    return true;
}

void PeerSession::setState(State state, const QString &detail)
{
    if (state_ == state) {
        return;
    }
    state_ = state;
    Q_EMIT stateChanged(state, detail);
}

void PeerSession::sendIdentify()
{
    // Role 0x03 = 双向（协议 §4.2 的 RoleBoth）。
    const proto::GoBuffer payload = proto::identifyPayload(
        localPlatform_, proto::role::kBoth, proto::caps::kDeepinDefault,
        static_cast<std::uint32_t>(QDateTime::currentSecsSinceEpoch()), localName_);
    if (payload.isNull()) {
        lastError_ = QStringLiteral("IDENTIFY 载荷构造失败");
        return;
    }
    identifySent_ = true;
    const quint64 timestamp =
        static_cast<quint64>(QDateTime::currentMSecsSinceEpoch()) * kTimestampUnit;
    const proto::GoBuffer wire = proto::encodeFrameWithHmac(
        kFrameIdentify, outSequence_++, timestamp, payload.toByteArray(), hmacKey_);
    if (!wire.isNull()) {
        writeRecord(wire.toByteArray());
    }
}

void PeerSession::sendIdentifyResponse(std::uint8_t status)
{
    const proto::GoBuffer payload = proto::identifyResponsePayload(
        status, localPlatform_, proto::caps::kDeepinDefault, localName_);
    if (payload.isNull()) {
        lastError_ = QStringLiteral("IDENTIFY_RESPONSE 载荷构造失败");
        return;
    }
    const quint64 timestamp =
        static_cast<quint64>(QDateTime::currentMSecsSinceEpoch()) * kTimestampUnit;
    const proto::GoBuffer wire = proto::encodeFrameWithHmac(
        kFrameIdentifyRes, outSequence_++, timestamp, payload.toByteArray(), hmacKey_);
    if (!wire.isNull()) {
        writeRecord(wire.toByteArray());
    }
}

void PeerSession::writeRecord(const QByteArray &frameBytes)
{
    if (socket_ == nullptr) {
        return;
    }
    // §7.2：2 字节大端长度前缀，覆盖帧体（含 HMAC 尾）。
    QByteArray record;
    record.reserve(kLengthPrefixBytes + frameBytes.size());
    const quint16 length = static_cast<quint16>(frameBytes.size());
    record.append(static_cast<char>((length >> 8) & 0xFF));
    record.append(static_cast<char>(length & 0xFF));
    record.append(frameBytes);
    socket_->write(record);
}

void PeerSession::processBuffer()
{
    while (true) {
        if (buffer_.size() < kLengthPrefixBytes) {
            return;
        }
        const auto *bytes = reinterpret_cast<const unsigned char *>(buffer_.constData());
        const int length = (static_cast<int>(bytes[0]) << 8) | static_cast<int>(bytes[1]);
        if (length <= 0 || length > kMaxRecordBytes) {
            // 长度前缀坏了：这条流已经无法对齐，丢掉整段重连，
            // 否则会在同一批坏字节上死循环。
            lastError_ = QStringLiteral("收到非法记录长度 %1").arg(length);
            resetConnection(lastError_);
            return;
        }
        if (buffer_.size() < kLengthPrefixBytes + length) {
            return; // 半条记录，等下次
        }
        const QByteArray record = buffer_.mid(kLengthPrefixBytes, length);
        buffer_.remove(0, kLengthPrefixBytes + length);

        proto::Frame frame;
        if (!proto::decodeFrameWithHmac(record, hmacKey_, &frame)) {
            // HMAC 不符或帧体损坏：跳过这一条，不能因为一条坏帧就断掉整条会话。
            continue;
        }

        switch (frame.type) {
        case kFrameIdentify:
            identifyReceived_ = true;
            if (!responseSent_) {
                // 被控端：等用户决定接受还是拒绝。
                setState(State::AwaitingAccept,
                         QStringLiteral("收到 %1 的连接请求").arg(peerAddress_));
                Q_EMIT inviteReceived(peerName_, peerAddress_);
            }
            break;
        case kFrameIdentifyRes:
            // 发起端：对端回应了。status 0 = 接受。
            if (frame.payload.size() >= 1) {
                const auto status = static_cast<std::uint8_t>(frame.payload.at(0));
                if (status == kIdentifyAccepted) {
                    setState(State::Connected,
                             QStringLiteral("已连接 %1").arg(peerAddress_));
                } else {
                    setState(State::Rejected, QStringLiteral("%1 拒绝了连接").arg(peerAddress_));
                }
            }
            break;
        default:
            Q_EMIT frameReceived(frame.type, frame.payload);
            break;
        }
    }
}

void PeerSession::dropSocket()
{
    // 摘指针 → 断回调 → abort → 延后释放，四步的顺序不能换：
    // abort() 会**同步**发 disconnected，回调里又会调 resetConnection()，
    // 若此时 socket_ 还指着同一个对象，就会出现「重入时又被 abort 一次」，
    // 更糟的是回到 close() 后 socket_ 已被回调置空，再 dereference 就是段错误。
    // 先把指针摘掉并断开会话，就切断了这条重入路径。
    QTcpSocket *socket = socket_;
    socket_ = nullptr;
    if (socket == nullptr) {
        return;
    }
    socket->disconnect(this);
    socket->abort();
    socket->deleteLater();
}

void PeerSession::resetConnection(const QString &reason)
{
    if (!reason.isEmpty()) {
        lastError_ = reason;
    }
    dropSocket();
    buffer_.clear();
    identifySent_ = false;
    identifyReceived_ = false;
    responseSent_ = false;
    outSequence_ = 0;
}

void PeerSession::onNewConnection()
{
    QTcpSocket *incoming = server_.nextPendingConnection();
    if (incoming == nullptr) {
        return;
    }
    // 一次只服务一个对端：旧连接直接踢掉。
    dropSocket();
    socket_ = incoming;
    buffer_.clear();
    peerAddress_ = QStringLiteral("%1:%2")
                       .arg(incoming->peerAddress().toString())
                       .arg(incoming->peerPort());
    connect(socket_, &QTcpSocket::disconnected, this, &PeerSession::onSocketDisconnected);
    connect(socket_, &QTcpSocket::errorOccurred, this, &PeerSession::onSocketError);
    connect(socket_, &QTcpSocket::readyRead, this, &PeerSession::onSocketReadyRead);
}

void PeerSession::onSocketConnected()
{
    if (socket_ == nullptr) {
        return;
    }
    peerAddress_ = QStringLiteral("%1:%2")
                       .arg(socket_->peerAddress().toString())
                       .arg(socket_->peerPort());
    // 发起方：连上就报身份，等对端接受/拒绝。
    setState(State::Handshaking, QStringLiteral("已连上 %1，等待对方接受").arg(peerAddress_));
    sendIdentify();
}

void PeerSession::onSocketDisconnected()
{
    // 已经处于「已拒绝」时不要被覆写成「连接已断开」——那是两件事：
    // 前者是对端的明确答复，后者是传输异常。
    if (state_ != State::Idle && state_ != State::Failed && state_ != State::Rejected) {
        setState(State::Failed, QStringLiteral("连接已断开"));
    }
    resetConnection(QString());
}

void PeerSession::onSocketError(QAbstractSocket::SocketError error)
{
    Q_UNUSED(error)
    if (socket_ == nullptr) {
        return;
    }
    lastError_ = socket_->errorString();
    setState(State::Failed, lastError_);
}

void PeerSession::onSocketReadyRead()
{
    if (socket_ == nullptr) {
        return;
    }
    buffer_.append(socket_->readAll());
    processBuffer();
}

} // namespace fg::input
