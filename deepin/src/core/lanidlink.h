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
#include 

namespace fg::proto {

class LanIDLink : public QObject {
    Q_OBJECT

public:
    LanIDLink(const Announcer &announcer, Listener &listener, QObject *parent = nullptr);
    ~LanIDLink() override;

    // 对端列表变化回调（listener 线程安全，UI 可直接读）。
    void setOnPeersChanged(std::function<void()> callback) { onPeersChanged_ = std::move(callback); }

    void start();
    void stop();

private slots:
    void onSendTimer();
    void onPruneTimer();
    void onReadyRead();

private:
    void sendAnnouncement();

    const Announcer &announcer_;
    Listener &listener_;
    QUdpSocket sendSocket_;
    QUdpSocket recvSocket_;
    QTimer sendTimer_;
    QTimer pruneTimer_;
    std::function<void()> onPeersChanged_;
    bool started_ = false;
};

} // namespace fg::proto

#endif // FLOWGRID_LANID_LINK_H
