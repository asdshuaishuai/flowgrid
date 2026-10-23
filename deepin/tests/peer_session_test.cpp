// 对端会话的端到端单测：真实 TCP 连接 + IDENTIFY 握手 + 帧往返。
//
// 跑的是真 socket（回环），但不需要 uinput 权限、不需要第二台机器：
// 被控端与控制端都在本进程里，只是两个 PeerSession 实例。
//
// 覆盖：
//   · 握手：控制端 dial → 被控端收到邀请 → 接受 → 双方进入 Connected
//   · 拒绝：被控端拒绝 → 控制端进入 Rejected
//   · 帧往返：控制端发 KEY_DOWN → 被控端收到同样的帧类型与载荷
//   · 配对码不一致 → 帧解不开（HMAC 不符），不会静默变成「收到空帧」
//
// 构建运行：
//   cmake --build deepin/build-dev --target flowgrid-peersessiontest
//   ./deepin/build-dev/flowgrid-peersessiontest
#include "core/input/peer_session.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QTimer>

#include <cstdio>
#include <cstdlib>

namespace {

int failures = 0;

void check(bool condition, const char *what)
{
    if (condition) {
        return;
    }
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
}

using namespace fg;

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    // ── ① 握手 + 帧往返 ────────────────────────────────────────────────
    {
        input::PeerSession server;
        input::PeerSession client;
        server.setLocalIdentity(QStringLiteral("被控端"), proto::platform::kLinux);
        client.setLocalIdentity(QStringLiteral("控制端"), proto::platform::kLinux);
        server.setPairingCode(QStringLiteral("1234"));
        client.setPairingCode(QStringLiteral("1234"));

        bool serverAccepted = false;
        bool gotFrame = false;
        std::uint8_t gotType = 0;
        QByteArray gotPayload;

        QObject::connect(&server, &input::PeerSession::inviteReceived,
                         [&server, &serverAccepted](const QString &, const QString &) {
                             serverAccepted = true;
                             server.respondInvite(true);
                         });
        QObject::connect(&server, &input::PeerSession::frameReceived,
                         [&](std::uint8_t type, const QByteArray &payload) {
                             gotType = type;
                             gotPayload = payload;
                             gotFrame = true;
                             app.quit();
                         });
        QObject::connect(&client, &input::PeerSession::stateChanged,
                         [&client](input::PeerSession::State state, const QString &) {
                             if (state == input::PeerSession::State::Connected) {
                                 client.sendFrame(proto::frametype::kKeyDown,
                                                  proto::keyDownPayload(0x04, 0x08).toByteArray());
                             }
                         });

        const quint16 port = 34901;
        check(server.listen(port), "监听成功");
        client.connectTo(QStringLiteral("127.0.0.1"), port);

        QElapsedTimer timer;
        timer.start();
        QTimer::singleShot(5000, &app, [&app]() { app.exit(3); });
        const int rc = app.exec();
        (void)rc;

        check(serverAccepted, "被控端收到了连接邀请");
        check(client.isConnected(), "控制端握手完成");
        check(server.isConnected(), "被控端握手完成");
        check(gotFrame, "被控端收到了帧");
        check(gotType == proto::frametype::kKeyDown, "帧类型正确");
        check(gotPayload.size() == 3, "帧载荷长度正确");
        if (gotPayload.size() == 3) {
            proto::KeyDownPayload parsed;
            check(proto::parseKeyDown(gotPayload, &parsed), "载荷可解析");
            check(parsed.keyCode == 0x04, "载荷键码正确");
            check(parsed.modifiers == 0x08, "载荷掩码正确");
        }
    }

    // ── ② 拒绝路径 ─────────────────────────────────────────────────────
    {
        input::PeerSession server;
        input::PeerSession client;
        server.setLocalIdentity(QStringLiteral("被控端"), proto::platform::kLinux);
        client.setLocalIdentity(QStringLiteral("控制端"), proto::platform::kLinux);
        server.setPairingCode(QStringLiteral("1234"));
        client.setPairingCode(QStringLiteral("1234"));

        bool rejected = false;
        QObject::connect(&server, &input::PeerSession::inviteReceived,
                         [&server](const QString &, const QString &) {
                             server.respondInvite(false);
                         });
        QObject::connect(&client, &input::PeerSession::stateChanged,
                         [&](input::PeerSession::State state, const QString &) {
                             if (state == input::PeerSession::State::Rejected) {
                                 rejected = true;
                                 app.quit();
                             }
                         });

        const quint16 port = 34902;
        check(server.listen(port), "监听成功");
        client.connectTo(QStringLiteral("127.0.0.1"), port);

        QTimer::singleShot(5000, &app, [&app]() { app.exit(3); });
        app.exec();

        check(rejected, "控制端收到拒绝");
        check(!client.isConnected(), "拒绝后没有进入已连接");
    }

    // ── ③ 配对码不一致 → 帧解不开（不能静默变成「收到空帧」）──────────
    {
        input::PeerSession server;
        input::PeerSession client;
        server.setLocalIdentity(QStringLiteral("被控端"), proto::platform::kLinux);
        client.setLocalIdentity(QStringLiteral("控制端"), proto::platform::kLinux);
        server.setPairingCode(QStringLiteral("1234"));
        client.setPairingCode(QStringLiteral("9999")); // 故意不一致

        bool gotFrame = false;
        QObject::connect(&server, &input::PeerSession::inviteReceived,
                         [&server](const QString &, const QString &) {
                             server.respondInvite(true);
                         });
        QObject::connect(&server, &input::PeerSession::frameReceived,
                         [&](std::uint8_t, const QByteArray &) {
                             gotFrame = true;
                             app.quit();
                         });
        QObject::connect(&client, &input::PeerSession::stateChanged,
                         [&client](input::PeerSession::State state, const QString &) {
                             if (state == input::PeerSession::State::Connected) {
                                 client.sendFrame(proto::frametype::kKeyDown,
                                                  proto::keyDownPayload(0x04, 0x00).toByteArray());
                             }
                         });

        const quint16 port = 34903;
        check(server.listen(port), "监听成功");
        client.connectTo(QStringLiteral("127.0.0.1"), port);

        QTimer::singleShot(1500, &app, [&app]() { app.exit(0); });
        app.exec();

        // 握手本身不带业务帧校验，所以握手仍会完成；发过去的业务帧因为
        // HMAC 对不上而被丢弃——这正是要验的行为。
        check(!gotFrame, "配对码不一致时业务帧被丢弃");
    }

    if (failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("peer_session_test: OK\n");
    return EXIT_SUCCESS;
}
