// 键鼠控制会话：把「发现 → 连接 → 捕获/注入」串成一条能真跑的链路。
//
// 各层职责（详见 docs/INPUT-CAPABILITY.md）：
//   LanIdDiscovery        发现对端（UDP 组播，§3.1）
//   input::PeerSession    TCP 通道 + IDENTIFY 握手（§7.2 / §4.2）
//   input::OsControl      捕获 / 注入的薄壳（真正的实现在协议层 oscontrol）
//
// 单例：同一时刻只有一个控制会话——跟 DeviceManager 一样是全局状态。
// 界面只跟这一个对象打交道，不直接碰 socket 与引擎。
//
// 线程：捕获线程取出**成品帧** → **排队回主线程**再发。
// QTcpSocket 不是线程安全的，捕获线程直接 write 会出事，所以那一跳必须走
// 事件循环队列。
//
// 注意这里已经没有「重映射 / 组帧 / 键位表」了：那些在协议层
// （protocol/oscontrol + protocol/oscontrol/evdev）。客户端只做 UI + 搬运。
#ifndef FLOWGRID_PEERCONTROLLER_H
#define FLOWGRID_PEERCONTROLLER_H

#include "core/input/peer_session.h"

#include <QObject>
#include <QString>

#include <cstdint>
#include <memory>

namespace fg {

namespace input {
class OsControl;
} // namespace input

class PeerController : public QObject {
    Q_OBJECT

public:
    static PeerController &instance();

    // 起发现 + 监听会话端口。幂等；失败原因在 lastError()。
    void start();
    void stop();

    // 主动连接对端。address 是「ip:port」，端口是对方的**会话端口**
    // （不是 LAN ID 的组播 UDP 端口）。
    void connectTo(const QString &address, const QString &name, std::uint8_t platform);

    // 对收到的连接邀请作答（接受 / 拒绝）。
    void respondInvite(bool accept);

    // 断开当前对端。
    void disconnectPeer();

    // 接管开关：开 = 捕获本机键鼠并转发给对端；关 = 释放，键鼠回到本机。
    //
    // 开之前过三关：有已连接的对端 / 有 uinput 权限 / 会话真的通了。
    // 任何一关不过就把开关拨回去并说清原因——留一个「显示为开、实际没接管」
    // 的开关比不做还糟。
    bool setCaptureEnabled(bool on);
    bool captureEnabled() const { return captureEnabled_; }

    // 被控端：正在接收并注入对端键鼠。
    bool injectionEnabled() const { return injectionEnabled_; }

    bool isConnected() const;
    // 本端在这次连接里的角色：本端发起 = 控制端（发键鼠），被邀请 = 被控端（收键鼠）。
    bool isControllerSide() const { return initiator_; }

    QString peerName() const { return peerName_; }
    QString statusText() const;
    QString lastError() const { return lastError_; }

Q_SIGNALS:
    // 连接状态变了（连接中 / 已连接 / 断开…）。界面据此刷新。
    void stateChanged();

    // 收到对端的连接请求，等用户决定。
    void inviteReceived(const QString &peerName, const QString &address);

    // 出错原因（已同时 toast 过，这里给界面做状态展示用）。
    void errorRaised(const QString &message);

private:
    PeerController();
    ~PeerController() override;

    void onSessionState(input::PeerSession::State state, const QString &detail);
    void onInboundFrame(std::uint8_t type, const QByteArray &payload);
    void onInvite(const QString &name, const QString &address);
    void setError(const QString &message);

    // 打开注入（被控端）：uinput 权限不足时如实失败。
    bool startInjection();
    void stopInjection();

    // 加载键位规则表并接进重映射通路。
    void applyKeyRules();

    std::unique_ptr<input::OsControl> osControl_;
    std::unique_ptr<input::PeerSession> session_;

    QString peerName_;
    QString peerAddress_;
    std::uint8_t peerPlatform_ = proto::platform::kLinux;
    bool initiator_ = false;   // 本端是否发起了这次连接
    bool captureEnabled_ = false;
    bool injectionEnabled_ = false;
    QString lastError_;
};

} // namespace fg

#endif // FLOWGRID_PEERCONTROLLER_H
