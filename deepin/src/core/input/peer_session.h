// 对端会话：TCP 通道 + §7.2 长度前缀 + IDENTIFY 握手（§4.2）。
//
// 线上格式与 mac 的 TCPFraming 完全一致：
//     Length(2B BE) + HID Frame(12+N) [+ 8B HMAC]
// Length 覆盖帧体与 HMAC 尾（§7.2）。会话 HMAC 密钥由**配对码派生**
// （与 mac 的 SessionKey.derive 同一算法：SHA256("FlowGrid-mvp-session|"+码)）——
// 两端算不出同一个密钥时，收到的帧会被判为不完整而丢弃，而且症状是
// 「连上了却一条帧都收不到」，极难查。所以这步不能省，也不能各自发挥。
//
// 分工（AGENTS.md §29）：传输归客户端，但帧的编解码、载荷、握手载荷的
// 构造与解析全部经 protocol/cgo（proto::encodeFrame / decodeFrame /
// identifyPayload），本文件只搬字节。
#ifndef FLOWGRID_INPUT_PEER_SESSION_H
#define FLOWGRID_INPUT_PEER_SESSION_H

#include "../protocolapi.h"

#include <QAbstractSocket>
#include <QByteArray>
#include <QObject>
#include <QString>
#include <QTcpServer>
#include <QTcpSocket>

#include <cstdint>

namespace fg::input {

// DirectLink 的 TCP 会话端口。**不能与 LAN ID 的组播端口 24801 相同**——
// 那是 UDP 端口，TCP 绑上去会与组播 socket 抢。
//
// 通告里报的就是这个端口：对端按通告里的端口 dial 过来（与 web demo 的
// 做法一致）。mac 目前通告的仍是 24801，那是 UDP 端口、拨不通——等 mac
// 侧也改成报会话端口，两端才能互通（见 docs/INPUT-CAPABILITY.md）。
inline constexpr quint16 kPeerSessionPort = 34801;

class PeerSession : public QObject {
    Q_OBJECT

public:
    enum class State {
        Idle,           // 没有连接
        Connecting,     // TCP 拨号中
        AwaitingAccept, // 收到对端 IDENTIFY，等用户接受/拒绝
        Handshaking,    // 本端发了 IDENTIFY，等对端回应
        Connected,      // 握手完成，可收发业务帧
        Rejected,       // 对端拒绝了连接
        Failed,         // 连接或握手失败
    };
    Q_ENUM(State)

    explicit PeerSession(QObject *parent = nullptr);
    ~PeerSession() override;

    // 本机身份：握手时要报给对端。
    void setLocalIdentity(const QString &name, std::uint8_t platform);

    // 配对码：派生会话 HMAC 密钥。两端必须一致。
    void setPairingCode(const QString &code);

    // 监听端口，等对端连进来（被控端）。
    bool listen(quint16 port);
    // 主动连接对端（控制端）。
    void connectTo(const QString &host, quint16 port);

    // 对收到的连接请求作出回应：accept=true 发 status 0（接受）。
    void respondInvite(bool accept);

    // 发一条业务帧。握手未完成时返回 false。
    bool sendFrame(std::uint8_t frameType, const QByteArray &payload);

    State state() const { return state_; }
    bool isConnected() const { return state_ == State::Connected; }
    QString peerName() const { return peerName_; }
    QString peerAddress() const { return peerAddress_; }
    QString lastError() const { return lastError_; }

    void close();

Q_SIGNALS:
    void stateChanged(State state, const QString &detail);
    // 对端发来连接请求，等用户决定接受还是拒绝。
    void inviteReceived(const QString &peerName, const QString &peerAddress);
    // 一条入站业务帧（已解出帧类型与载荷）。
    void frameReceived(std::uint8_t frameType, const QByteArray &payload);

private Q_SLOTS:
    void onNewConnection();
    void onSocketConnected();
    void onSocketDisconnected();
    void onSocketError(QAbstractSocket::SocketError error);
    void onSocketReadyRead();

private:
    void setState(State state, const QString &detail);
    void sendIdentify();
    void sendIdentifyResponse(std::uint8_t status);
    void writeRecord(const QByteArray &frameBytes);
    // 从缓冲区切出完整记录并解帧；buffer_ 里剩下的半条留到下次。
    void processBuffer();
    void resetConnection(const QString &reason);
    // 断开并释放当前 socket；顺序见实现里的注释（abort 会同步发信号）。
    void dropSocket();

    QTcpServer server_;
    QTcpSocket *socket_ = nullptr;
    QByteArray buffer_;

    State state_ = State::Idle;
    QString localName_;
    std::uint8_t localPlatform_ = proto::platform::kLinux;
    QByteArray hmacKey_;
    QString peerName_;
    QString peerAddress_;
    std::uint16_t outSequence_ = 0;
    bool identifySent_ = false;
    bool identifyReceived_ = false;
    bool responseSent_ = false;
    QString lastError_;
};

} // namespace fg::input

#endif // FLOWGRID_INPUT_PEER_SESSION_H
