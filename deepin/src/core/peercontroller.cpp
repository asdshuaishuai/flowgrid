#include "core/peercontroller.h"

#include "core/hostinfo.h"
#include "core/input/input_forwarder.h"
#include "core/laniddiscovery.h"
#include "core/settings.h"

#include <QHostAddress>
#include <QMetaObject>
#include <QTimer>

namespace fg {

namespace {

// 「ip:port」→ 主机与端口。解析不出来返回 false。
bool splitAddress(const QString &address, QString *outHost, quint16 *outPort)
{
    const int colon = address.lastIndexOf(QLatin1Char(':'));
    if (colon <= 0 || colon + 1 >= address.size()) {
        return false;
    }
    const QString host = address.left(colon);
    bool ok = false;
    const uint port = address.mid(colon + 1).toUInt(&ok);
    if (!ok || port == 0 || port > 65535) {
        return false;
    }
    if (outHost != nullptr) {
        *outHost = host;
    }
    if (outPort != nullptr) {
        *outPort = static_cast<quint16>(port);
    }
    return true;
}

} // namespace

PeerController &PeerController::instance()
{
    static PeerController controller;
    return controller;
}

PeerController::PeerController()
{
    engine_ = std::make_unique<input::Engine>();
    session_ = std::make_unique<input::PeerSession>();
    forwarder_ = std::make_unique<input::Forwarder>(engine_.get(), nullptr);

    // 发送回调：捕获线程 → 主线程。QTcpSocket 不是线程安全的，
    // 捕获线程直接 write 会出事，这一跳必须走事件循环队列。
    forwarder_->setSendFn([this](const input::OutboundFrame &frame) {
        QMetaObject::invokeMethod(
            this,
            [this, frame]() {
                if (session_->isConnected()) {
                    session_->sendFrame(frame.frameType, frame.payload);
                }
            },
            Qt::QueuedConnection);
    });

    connect(session_.get(), &input::PeerSession::stateChanged, this,
            &PeerController::onSessionState);
    connect(session_.get(), &input::PeerSession::frameReceived, this,
            &PeerController::onInboundFrame, Qt::QueuedConnection);
    connect(session_.get(), &input::PeerSession::inviteReceived, this,
            &PeerController::onInvite);
}

PeerController::~PeerController()
{
    stop();
}

void PeerController::start()
{
    LanIdDiscovery::instance().start();

    session_->setLocalIdentity(hostinfo::hostName(), proto::platform::kLinux);
    // 会话密钥由配对码派生：两端算不出同一把就收不到对方的帧。
    session_->setPairingCode(Settings::instance().pairingCode());

    if (!session_->listen(input::kPeerSessionPort)) {
        setError(tr("无法监听会话端口 %1").arg(input::kPeerSessionPort));
    }
}

void PeerController::stop()
{
    if (captureEnabled_) {
        setCaptureEnabled(false);
    }
    if (injectionEnabled_) {
        stopInjection();
    }
    session_->close();
}

void PeerController::connectTo(const QString &address, const QString &name,
                               std::uint8_t platform)
{
    QString host;
    quint16 port = 0;
    if (!splitAddress(address, &host, &port)) {
        setError(tr("对端地址无法解析：%1").arg(address));
        return;
    }
    peerAddress_ = address;
    peerName_ = name;
    peerPlatform_ = platform;
    initiator_ = true;
    forwarder_->setPeerPlatform(platform);

    session_->setLocalIdentity(hostinfo::hostName(), proto::platform::kLinux);
    session_->setPairingCode(Settings::instance().pairingCode());
    session_->connectTo(host, port);
    Q_EMIT stateChanged();
}

void PeerController::respondInvite(bool accept)
{
    session_->respondInvite(accept);
    if (!accept) {
        // 拒绝了就把这次连接的对端信息清掉：状态胶囊不该还显示着那个人。
        peerName_.clear();
        peerAddress_.clear();
        initiator_ = false;
        Q_EMIT stateChanged();
    }
}

void PeerController::disconnectPeer()
{
    if (captureEnabled_) {
        setCaptureEnabled(false);
    }
    session_->close();
    peerName_.clear();
    peerAddress_.clear();
    initiator_ = false;
    Q_EMIT stateChanged();
}

bool PeerController::setCaptureEnabled(bool on)
{
    if (!on) {
        captureEnabled_ = false;
        engine_->stopCapture();
        // 停转发前把还按着的键松开：对端在另一台机器上，我们不再发 keyUp
        // 的话那边的键会一直卡在按下态。
        forwarder_->releasePressed();
        Q_EMIT stateChanged();
        return true;
    }

    // 三关：有对端 / 会话已通 / uinput 权限。缺哪关都说清楚。
    if (!session_->isConnected() || peerName_.isEmpty()) {
        setError(tr("未连接对端，无法接管键鼠"));
        return false;
    }

    std::string error;
    const bool started = engine_->startCapture(
        [this](const input::CapturedEvent &event) {
            // 捕获线程 → 主线程：重映射与组帧都在主线程做，
            // 免得 Forwarder 的状态字段被两个线程同时碰。
            QMetaObject::invokeMethod(
                this,
                [this, event]() { forwarder_->forwardCaptured(event); },
                Qt::QueuedConnection);
        },
        true, // grab：控制端语义，事件不再落到本机
        &error);
    if (!started) {
        setError(error.empty() ? tr("捕获引擎启动失败")
                               : QString::fromStdString(error));
        captureEnabled_ = false;
        Q_EMIT stateChanged();
        return false;
    }
    captureEnabled_ = true;
    Q_EMIT stateChanged();
    return true;
}

bool PeerController::isConnected() const
{
    return session_->isConnected();
}

QString PeerController::statusText() const
{
    if (session_->isConnected() && !peerName_.isEmpty()) {
        return tr("已连接 %1").arg(peerName_);
    }
    if (session_->state() == input::PeerSession::State::Connecting) {
        return tr("正在连接 %1").arg(peerAddress_);
    }
    if (session_->state() == input::PeerSession::State::AwaitingAccept) {
        return tr("等待对方接受连接");
    }
    if (LanIdDiscovery::instance().isRunning()) {
        return tr("未连接 · 监听 %1").arg(input::kPeerSessionPort);
    }
    return tr("未连接");
}

void PeerController::onSessionState(input::PeerSession::State state,
                                    const QString &detail)
{
    switch (state) {
    case input::PeerSession::State::Connected:
        // 被控端（收到邀请的一方）这时开始注入对端发来的键鼠。
        if (!initiator_) {
            startInjection();
        }
        break;
    case input::PeerSession::State::Rejected:
        peerName_.clear();
        peerAddress_.clear();
        initiator_ = false;
        setError(detail);
        break;
    case input::PeerSession::State::Failed:
        if (captureEnabled_) {
            setCaptureEnabled(false);
        }
        stopInjection();
        peerName_.clear();
        peerAddress_.clear();
        initiator_ = false;
        setError(detail);
        break;
    default:
        break;
    }
    Q_EMIT stateChanged();
}

void PeerController::onInboundFrame(std::uint8_t type, const QByteArray &payload)
{
    forwarder_->handleInbound(type, payload);
}

void PeerController::onInvite(const QString &name, const QString &address)
{
    peerName_ = name;
    peerAddress_ = address;
    initiator_ = false;
    Q_EMIT inviteReceived(name, address);
    Q_EMIT stateChanged();
}

bool PeerController::startInjection()
{
    if (injectionEnabled_) {
        return true;
    }
    std::string error;
    if (!engine_->startInjection(&error)) {
        setError(error.empty() ? tr("注入引擎启动失败")
                               : QString::fromStdString(error));
        injectionEnabled_ = false;
        return false;
    }
    injectionEnabled_ = true;
    return true;
}

void PeerController::stopInjection()
{
    if (!injectionEnabled_) {
        return;
    }
    engine_->releaseAll();
    engine_->stopInjection();
    injectionEnabled_ = false;
}

void PeerController::setError(const QString &message)
{
    lastError_ = message;
    Q_EMIT errorRaised(message);
}

} // namespace fg
