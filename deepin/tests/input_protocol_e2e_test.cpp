// 协议级真实操作测试：走完整条链路，只把 OS HAL 换成假的。
//
// 链路：真实捕获事件序列 → Forwarder（协议层重映射）→ PeerSession（真实 TCP
//       + §7.2 长度前缀 + HMAC）→ 对端 PeerSession → Forwarder（解析）→ 注入
//
// 场景是**一次真实的键盘操作**：按住 Ctrl，点一下 C，再松开。
// 断言对端收到的动作序列与「macOS 上按了 Cmd+C」完全一致——
// 这条链路里任何一环错位（重映射漏了、掩码丢了、帧错位了）都会在这里露馅。
//
// 唯一没真的是 evdev/uinput：那两个要 root 权限，见 input_hal_test.cpp。
#include "core/input/input_forwarder.h"
#include "core/input/peer_session.h"

#include <QCoreApplication>
#include <QTimer>

#include <cstdio>
#include <cstdlib>
#include <vector>

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
using namespace fg::input;

// 记录对端真正收到的注入动作。
class RecordingInjector : public Injector {
public:
    struct KeyCall {
        std::uint16_t hid;
        bool down;
        std::uint8_t mods;
    };

    bool injectKey(std::uint16_t hid, bool down, std::uint8_t modifiers) override
    {
        keys.push_back(KeyCall{hid, down, modifiers});
        return true;
    }
    bool injectMouseMove(std::int16_t dx, std::int16_t dy) override
    {
        moves.push_back({dx, dy});
        return true;
    }
    bool injectMouseButton(std::uint8_t button, bool down) override
    {
        buttons.push_back({button, down});
        return true;
    }
    bool injectScroll(std::int16_t deltaY, std::int16_t deltaX) override
    {
        scrolls.push_back({deltaY, deltaX});
        return true;
    }

    std::vector<KeyCall> keys;
    std::vector<std::pair<std::int16_t, std::int16_t>> moves;
    std::vector<std::pair<std::uint8_t, bool>> buttons;
    std::vector<std::pair<std::int16_t, std::int16_t>> scrolls;
};

// HID Usage（§6.1）。
constexpr std::uint16_t kHidC = 0x06;
constexpr std::uint16_t kHidLCtrl = 0xE0;
constexpr std::uint16_t kHidLMeta = 0xE3;

// 协议 §6.2 修饰位。
constexpr std::uint8_t kModLeftCtrl = 0x01;
constexpr std::uint8_t kModLeftMeta = 0x08;

// 一次真实的键盘操作：按住 Ctrl → 点 C → 松 C → 松 Ctrl。
// 掩码按捕获线程的真实时序给：修饰键的位在事件发出前就已经置上。
std::vector<CapturedEvent> ctrlCOnLinux()
{
    std::vector<CapturedEvent> seq;
    CapturedEvent ctrlDown;
    ctrlDown.kind = EventKind::KeyDown;
    ctrlDown.keyCode = kHidLCtrl;
    ctrlDown.modifiers = kModLeftCtrl;
    seq.push_back(ctrlDown);

    CapturedEvent cDown;
    cDown.kind = EventKind::KeyDown;
    cDown.keyCode = kHidC;
    cDown.modifiers = kModLeftCtrl;
    seq.push_back(cDown);

    CapturedEvent cUp;
    cUp.kind = EventKind::KeyUp;
    cUp.keyCode = kHidC;
    cUp.modifiers = kModLeftCtrl;
    seq.push_back(cUp);

    CapturedEvent ctrlUp;
    ctrlUp.kind = EventKind::KeyUp;
    ctrlUp.keyCode = kHidLCtrl;
    ctrlUp.modifiers = 0;
    seq.push_back(ctrlUp);

    return seq;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    // ── Linux 控制端 → macOS 被控端：Ctrl+C 应当变成 Cmd+C ─────────────
    {
        PeerSession target;
        PeerSession controller;
        RecordingInjector targetInjector;

        target.setLocalIdentity(QStringLiteral("mac-mini"), proto::platform::kMacOS);
        controller.setLocalIdentity(QStringLiteral("deepin-pc"), proto::platform::kLinux);
        // 两端配对码一致 → 会话 HMAC 密钥一致 → 帧才解得开。
        target.setPairingCode(QStringLiteral("4821"));
        controller.setPairingCode(QStringLiteral("4821"));

        Forwarder sender(&targetInjector, nullptr);
        sender.setPeerPlatform(proto::platform::kMacOS);
        sender.setSendFn([&controller](const OutboundFrame &frame) {
            controller.sendFrame(frame.frameType, frame.payload);
        });

        Forwarder receiver(&targetInjector, nullptr);
        QObject::connect(&target, &PeerSession::frameReceived, &app,
                         [&receiver](std::uint8_t type, const QByteArray &payload) {
                             receiver.handleInbound(type, payload);
                         });
        QObject::connect(&target, &PeerSession::inviteReceived, &target,
                         [&target](const QString &, const QString &) {
                             target.respondInvite(true);
                         });

        bool replayed = false;
        QObject::connect(&controller, &PeerSession::stateChanged, &controller,
                         [&](PeerSession::State state, const QString &) {
                             if (state != PeerSession::State::Connected || replayed) {
                                 return;
                             }
                             replayed = true;
                             // 握手一通，立刻把「用户按住 Ctrl 点了一下 C」重放出去。
                             for (const CapturedEvent &event : ctrlCOnLinux()) {
                                 sender.forwardCaptured(event);
                             }
                         });

        const quint16 port = 34911;
        check(target.listen(port), "被控端监听成功");
        controller.connectTo(QStringLiteral("127.0.0.1"), port);

        QTimer::singleShot(5000, &app, [&app]() { app.exit(3); });
        app.exec();

        // 对端应该收到的是「macOS 上按了 Cmd+C」：
        //   Meta down(带 Meta 掩码) → C down(带 Meta 掩码) → C up → Meta up
        check(targetInjector.keys.size() == 4, "对端收到 4 个按键动作");
        if (targetInjector.keys.size() == 4) {
            const auto &k = targetInjector.keys;
            check(k[0].hid == kHidLMeta && k[0].down, "第 1 个动作：按下 Meta");
            check(k[0].mods == kModLeftMeta, "第 1 个动作：掩码是 Meta");
            check(k[1].hid == kHidC && k[1].down, "第 2 个动作：按下 C");
            check(k[1].mods == kModLeftMeta, "第 2 个动作：掩码是 Meta（不是 Ctrl）");
            check(k[2].hid == kHidC && !k[2].down, "第 3 个动作：松开 C");
            check(k[3].hid == kHidLMeta && !k[3].down, "第 4 个动作：松开 Meta");
        }
    }

    // ── 同平台（Linux → Linux）：原样过去，不该有任何换算 ───────────────
    {
        PeerSession target;
        PeerSession controller;
        RecordingInjector targetInjector;

        target.setLocalIdentity(QStringLiteral("linux-b"), proto::platform::kLinux);
        controller.setLocalIdentity(QStringLiteral("linux-a"), proto::platform::kLinux);
        target.setPairingCode(QStringLiteral("4821"));
        controller.setPairingCode(QStringLiteral("4821"));

        Forwarder sender(&targetInjector, nullptr);
        sender.setPeerPlatform(proto::platform::kLinux);
        sender.setSendFn([&controller](const OutboundFrame &frame) {
            controller.sendFrame(frame.frameType, frame.payload);
        });

        Forwarder receiver(&targetInjector, nullptr);
        QObject::connect(&target, &PeerSession::frameReceived, &app,
                         [&receiver](std::uint8_t type, const QByteArray &payload) {
                             receiver.handleInbound(type, payload);
                         });
        QObject::connect(&target, &PeerSession::inviteReceived, &target,
                         [&target](const QString &, const QString &) {
                             target.respondInvite(true);
                         });

        bool replayed = false;
        QObject::connect(&controller, &PeerSession::stateChanged, &controller,
                         [&](PeerSession::State state, const QString &) {
                             if (state != PeerSession::State::Connected || replayed) {
                                 return;
                             }
                             replayed = true;
                             for (const CapturedEvent &event : ctrlCOnLinux()) {
                                 sender.forwardCaptured(event);
                             }
                         });

        const quint16 port = 34912;
        check(target.listen(port), "被控端监听成功");
        controller.connectTo(QStringLiteral("127.0.0.1"), port);

        QTimer::singleShot(5000, &app, [&app]() { app.exit(3); });
        app.exec();

        check(targetInjector.keys.size() == 4, "对端收到 4 个按键动作");
        if (targetInjector.keys.size() == 4) {
            const auto &k = targetInjector.keys;
            check(k[0].hid == kHidLCtrl && k[0].down, "同平台：按下 Ctrl（不换算）");
            check(k[0].mods == kModLeftCtrl, "同平台：掩码仍是 Ctrl");
            check(k[1].hid == kHidC && k[1].mods == kModLeftCtrl, "同平台：C 带 Ctrl 掩码");
            check(k[3].hid == kHidLCtrl && !k[3].down, "同平台：松开 Ctrl");
        }
    }

    // ── 鼠标：移动 + 按下 + 抬起 ───────────────────────────────────────
    {
        PeerSession target;
        PeerSession controller;
        RecordingInjector targetInjector;

        target.setLocalIdentity(QStringLiteral("mac-mini"), proto::platform::kMacOS);
        controller.setLocalIdentity(QStringLiteral("deepin-pc"), proto::platform::kLinux);
        target.setPairingCode(QStringLiteral("4821"));
        controller.setPairingCode(QStringLiteral("4821"));

        Forwarder sender(&targetInjector, nullptr);
        sender.setSendFn([&controller](const OutboundFrame &frame) {
            controller.sendFrame(frame.frameType, frame.payload);
        });

        Forwarder receiver(&targetInjector, nullptr);
        QObject::connect(&target, &PeerSession::frameReceived, &app,
                         [&receiver](std::uint8_t type, const QByteArray &payload) {
                             receiver.handleInbound(type, payload);
                         });
        QObject::connect(&target, &PeerSession::inviteReceived, &target,
                         [&target](const QString &, const QString &) {
                             target.respondInvite(true);
                         });

        bool replayed = false;
        QObject::connect(&controller, &PeerSession::stateChanged, &controller,
                         [&](PeerSession::State state, const QString &) {
                             if (state != PeerSession::State::Connected || replayed) {
                                 return;
                             }
                             replayed = true;

                             CapturedEvent move;
                             move.kind = EventKind::MouseMove;
                             move.deltaX = 12;
                             move.deltaY = -7;
                             sender.forwardCaptured(move);

                             CapturedEvent down;
                             down.kind = EventKind::MouseButton;
                             down.button = 0x01;
                             down.pressed = true;
                             sender.forwardCaptured(down);

                             CapturedEvent up;
                             up.kind = EventKind::MouseButton;
                             up.button = 0x01;
                             up.pressed = false;
                             sender.forwardCaptured(up);

                             CapturedEvent scroll;
                             scroll.kind = EventKind::Scroll;
                             scroll.deltaY = 3;
                             sender.forwardCaptured(scroll);
                         });

        const quint16 port = 34913;
        check(target.listen(port), "被控端监听成功");
        controller.connectTo(QStringLiteral("127.0.0.1"), port);

        QTimer::singleShot(5000, &app, [&app]() { app.exit(3); });
        app.exec();

        check(targetInjector.moves.size() == 1, "对端收到一次鼠标移动");
        if (targetInjector.moves.size() == 1) {
            check(targetInjector.moves.front().first == 12, "移动 dx 正确");
            check(targetInjector.moves.front().second == -7, "移动 dy 正确");
        }
        check(targetInjector.buttons.size() == 2, "对端收到两次鼠标按钮（按下 + 抬起）");
        if (targetInjector.buttons.size() == 2) {
            check(targetInjector.buttons[0].second, "第 1 次是按下");
            check(!targetInjector.buttons[1].second, "第 2 次是抬起");
            check(targetInjector.buttons[0].first == 0x01, "按钮号正确");
        }
        check(targetInjector.scrolls.size() == 1, "对端收到一次滚轮");
        if (targetInjector.scrolls.size() == 1) {
            check(targetInjector.scrolls.front().first == 3, "滚轮 deltaY 正确");
        }
    }

    if (failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("input_protocol_e2e_test: OK\n");
    return EXIT_SUCCESS;
}
