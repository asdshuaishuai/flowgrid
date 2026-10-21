#include "lanidlink.h"

#include <QNetworkDatagram>

namespace fg::proto {

// 多播参数（PROTOCOL.md §3.1.1；与 protocol/lanid.go 的常量一致）。
namespace {
constexpr const char *kGroup = "239.255.42.99";
constexpr quint16 kPort = 24801;
constexpr int kAnnouncePeriodMs = 2000;
constexpr int kPrunePeriodMs = 10000;
constexpr int kMaxAgeMs = 10000;
} // namespace

LanIDLink::LanIDLink(const Announcer &announcer, Listener &listener, QObject *parent)
    : QObject(parent)
    , announcer_(announcer)
    , listener_(listener)
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
    started_ = true;

    // 发送 socket：多播 TTL=1（不出局域网）
    sendSocket_.setMulticastTtl(1);

    // 接收 socket：绑定端口并加入多播组
    if (!recvSocket_.bind(QHostAddress::AnyIPv4, kPort, QUdpSocket::ShareAddress)) {
        return;
    }
    recvSocket_.joinMulticastGroup(QHostAddress(QString::fromLatin1(kGroup)));

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

} // namespace fg::proto
