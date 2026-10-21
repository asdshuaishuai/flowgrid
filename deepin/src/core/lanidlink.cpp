#include "lanidlink.h"

#include <QNetworkDatagram>
#include <QVariant>

namespace fg::proto {

// 多播参数（PROTOCOL.md §3.1.1；与 protocol/lanid.go 的常量一致）。
namespace {
constexpr const char *kGroup = "239.255.42.99";
constexpr quint16 kPort = 24801;
constexpr int kAnnouncePeriodMs = 2000;
constexpr int kPrunePeriodMs = 10000;
constexpr int kMaxAgeMs = 10000;
} // namespace

LanIDLink::LanIDLink(const Announcer &announcer, Listener &listener, const QByteArray &selfPublicKey,
                     QObject *parent)
    : QObject(parent)
    , announcer_(announcer)
    , listener_(listener)
    , selfPublicKey_(selfPublicKey)
{
    sendTimer_.setInterval(kAnnouncePeriodMs);
    connect(&sendTimer_, &QTimer::timeout, this, &LanIDLink::onSendTimer);

    pruneTimer_.setInterval(kPrunePeriodMs);
    connect(&pruneTimer_, &QTimer::timeout, this, &LanIDLink::onPruneTimer);

    connect(&recvSocket_, &QUdpSocket::readyRead, this, &LanIDLink::onReadyRead);
}

LanIDLink::~LanIDLink()
{
    stop();
}

void LanIDLink::start()
{
    if (started_) {
        return;
    }

    // 发送 socket：多播 TTL=1（不出局域网）。
    //
    // QUdpSocket 没有 setMulticastTtl / setMulticastLoopback 这类便捷方法，
    // 只有基类 QAbstractSocket::setSocketOption。这里原先写的是
    // sendSocket_.setMulticastTtl(1)——那个函数根本不存在，编译直接报错。
    // 它没被发现是因为这个文件从来没进过 CMakeLists 的源文件列表（openspec
    // intelligentlink 任务 4.3 就是补这一条）。
    sendSocket_.setSocketOption(QAbstractSocket::MulticastTtlOption, QVariant(1));
    // 多播回环：关掉。Linux 的 IP_MULTICAST_LOOP 默认开着，自己发的通告会被
    // 自己的接收 socket 收回来，协议层的对端表里就混进了「自己」。
    // 在发送侧掐掉是根治；peer() 里那道软件过滤是第二道保险（有些网卡/路由
    // 仍会把包绕回来）。
    sendSocket_.setSocketOption(QAbstractSocket::MulticastLoopbackOption, QVariant(0));

    // 接收 socket：绑定端口并加入多播组。
    // 绑不上/加不进组时保持「未启动」——早先把 started_ 提前置 true，
    // 失败之后 start() 会因「已在运行」直接返回，再也重试不了。
    if (!recvSocket_.bind(QHostAddress::AnyIPv4, kPort, QUdpSocket::ShareAddress)) {
        return;
    }
    if (!recvSocket_.joinMulticastGroup(QHostAddress(QString::fromLatin1(kGroup)))) {
        recvSocket_.close();
        return;
    }

    started_ = true;
    sendTimer_.start();
    pruneTimer_.start();
    onSendTimer(); // 立即发一条
}

void LanIDLink::stop()
{
    if (!started_) {
        return;
    }
    started_ = false;
    sendTimer_.stop();
    pruneTimer_.stop();
    recvSocket_.leaveMulticastGroup(QHostAddress(QString::fromLatin1(kGroup)));
    recvSocket_.close();
    sendSocket_.close();
}

void LanIDLink::onSendTimer()
{
    sendAnnouncement();
}

void LanIDLink::sendAnnouncement()
{
    const QByteArray wire = announcer_.announce();
    if (wire.isEmpty()) {
        return;
    }
    sendSocket_.writeDatagram(wire, QHostAddress(QString::fromLatin1(kGroup)), kPort);
}

void LanIDLink::onReadyRead()
{
    while (recvSocket_.hasPendingDatagrams()) {
        QNetworkDatagram datagram = recvSocket_.receiveDatagram();
        if (!datagram.isValid()) {
            continue;
        }
        // 源 IP：toIPv4Address 返回主机序 quint32，提取为网络序 4 字节
        const quint32 addr = datagram.senderAddress().toIPv4Address();
        QByteArray ipBytes(4, 0);
        ipBytes[0] = static_cast<char>((addr >> 24) & 0xFF);
        ipBytes[1] = static_cast<char>((addr >> 16) & 0xFF);
        ipBytes[2] = static_cast<char>((addr >> 8) & 0xFF);
        ipBytes[3] = static_cast<char>(addr & 0xFF);

        listener_.feed(datagram.data(), ipBytes);
    }
    if (onPeersChanged_) {
        onPeersChanged_();
    }
}

void LanIDLink::onPruneTimer()
{
    listener_.prune(kMaxAgeMs);
    if (onPeersChanged_) {
        onPeersChanged_();
    }
}

// 对端列表（已滤掉自己）。
//
// Listener 里的下标是**原始**下标（含自己那一条），这里给出去的是过滤后的
// 下标：调用方按 0..peerCount()-1 顺序读，拿到的就是真实对端，不会读到
// 本机自己。
int LanIDLink::peerCount() const
{
    int count = 0;
    const int total = listener_.peersCount();
    for (int i = 0; i < total; ++i) {
        LanIDPeer p;
        if (!listener_.peer(i, &p)) {
            continue;
        }
        if (!selfPublicKey_.isEmpty() && p.publicKey == selfPublicKey_) {
            continue;   // 多播回环到自己
        }
        ++count;
    }
    return count;
}

bool LanIDLink::peer(int index, LanIDPeer *out) const
{
    if (out == nullptr || index < 0) {
        return false;
    }
    const int total = listener_.peersCount();
    int seen = 0;
    for (int i = 0; i < total; ++i) {
        LanIDPeer p;
        if (!listener_.peer(i, &p)) {
            continue;
        }
        if (!selfPublicKey_.isEmpty() && p.publicKey == selfPublicKey_) {
            continue;
        }
        if (seen == index) {
            *out = p;
            return true;
        }
        ++seen;
    }
    return false;
}

} // namespace fg::proto
