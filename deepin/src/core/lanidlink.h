// 智能连接 UDP 多播适配器（deepin，PROTOCOL.md §3.1）。
// 持有 QUdpSocket，周期发送签名通告，收包喂 Listener，定时 Prune。
// 协议逻辑全在 fg::proto（cgo），本类只做 I/O 管道——与 mac 的 LanIDLink.swift、
// Windows 的 internal/lanid/lanid.go 三端对应。
#ifndef FLOWGRID_LANID_LINK_H
#define FLOWGRID_LANID_LINK_H

#include "protocolapi.h"

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QUdpSocket>
#include <QTimer>
#include <QHostAddress>

#include <cstdint>
#include <functional>

namespace fg::proto {

class LanIDLink : public QObject {
    Q_OBJECT

public:
    // selfPublicKey 是本机身份公钥（65 字节 SEC1，取自 Identity::publicKey()）。
    //
    // 多播会回环：自己发出去的通告很可能被自己的接收 socket 收回来，于是协议层
    // 的 Listener 里混进了「自己」这一条。不滤掉的话，界面会把自己列成一台
    // 可连接的对端，用户能对自己发起连接。
    // 协议层的 Listener 不知道本机公钥（它只负责解析），所以这个过滤只能做在
    // 客户端适配层——Windows 的 internal/lanid/lanid.go 就是这么做的
    // （selfKey 字段 + 比对 PubKey）。
    LanIDLink(const Announcer &announcer, Listener &listener, const QByteArray &selfPublicKey,
              QObject *parent = nullptr);
    ~LanIDLink() override;

    // 对端列表变化回调（listener 线程安全，UI 可直接读）。
    void setOnPeersChanged(std::function<void()> callback) { onPeersChanged_ = std::move(callback); }

    void start();
    void stop();

    // 对端列表（已滤掉本机自己）。索引是**过滤后**的下标，
    // 与 Listener::peer(index) 的原始下标不是一回事。
    int peerCount() const;
    bool peer(int index, LanIDPeer *out) const;

private slots:
    void onSendTimer();
    void onPruneTimer();
    void onReadyRead();

private:
    void sendAnnouncement();

    const Announcer &announcer_;
    Listener &listener_;
    QByteArray selfPublicKey_;
    QUdpSocket sendSocket_;
    QUdpSocket recvSocket_;
    QTimer sendTimer_;
    QTimer pruneTimer_;
    std::function<void()> onPeersChanged_;
    bool started_ = false;
};

} // namespace fg::proto

#endif // FLOWGRID_LANID_LINK_H
