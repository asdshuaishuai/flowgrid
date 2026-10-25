// 协议级真实操作测试：走完整条链路，只把 OS HAL 换成假的。
//
// 链路：捕获事件序列 → 协议层编码（fg_input_encode）→ PeerSession（真实 TCP
//       + §7.2 长度前缀 + HMAC）→ 对端 PeerSession → 协议层解码（parse*）→ 记录
//
// 场景是**一次真实的键盘操作**：按住 Ctrl，点一下 C，再松开。
// 断言对端收到的动作序列与「macOS 上按了 Cmd+C」完全一致——
// 这条链路里任何一环错位（重映射漏了、掩码丢了、帧错位了）都会在这里露馅。
//
// 迁移到 oscontrol 之后的分工：捕获与注入的**实现**在协议层
// （protocol/oscontrol + protocol/oscontrol/evdev），本测试盯的是
// **两端之间的那条链路**：编码 → 传输 → 解码。
//
// 唯一没真的是 evdev/uinput：那两个要权限，见 input_hal_test.cpp。
#include "core/input/peer_session.h"
#include "core/protocolapi.h"

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

// 记录对端真正收到的动作。
//
// 解码走协议层的 parse*（cgo → protocol.DecodeInput），所以这里记到的就是
// 协议层认为的语义——注入那一步换成了记录，语义不变。
class RecordingDecoder {
public:
    struct KeyCall {
        std::uint16_t hid = 0;
        bool down = false;
        std::uint8_t mods = 0;
    };

    void handle(std::uint8_t frameType, const QByteArray &payload)
    {
        switch (frameType) {
        case proto::frametype::kKeyDown: {
            proto::KeyDownPayload parsed;
            if (proto::parseKeyDown(payload, &parsed)) {
                keys.push_back(KeyCall{parsed.keyCode, true, parsed.modifiers});
            }
            break;
        }
        case proto::frametype::kKeyUp: {
            proto::KeyUpPayload parsed;
            if (proto::parseKeyUp(payload, &parsed)) {
                keys.push_back(KeyCall{parsed.keyCode, false, 0});
            }
            break;
        }
        case proto::frametype::kMouseMove: {
            proto::MouseMovePayload parsed;
            if (proto::parseMouseMove(payload, &parsed)) {
                moves.push_back({parsed.deltaX, parsed.deltaY});
            }
            break;
        }
        case proto::frametype::kMouseButton: {
            proto::MouseButtonPayload parsed;
            if (proto::parseMouseButton(payload, &parsed)) {
                buttons.push_back({parsed.buttonId, parsed.down});
            }
            break;
        }
        case proto::frametype::kScroll: {
            proto::ScrollPayload parsed;
            if (proto::parseScroll(payload, &parsed)) {
                scrolls.push_back({parsed.deltaY, parsed.deltaX});
            }
            break;
        }
        default:
            break;
        }
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
struct RawEvent {
    std::uint8_t kind = 0;
    std::uint16_t key = 0;
    std::uint8_t modifiers = 0;
    std::int16_t deltaX = 0;
    std::int16_t deltaY = 0;
    std::uint8_t button = 0;
    bool pressed = false;
};

std::vector<RawEvent> ctrlCOnLinux()
{
    std::vector<RawEvent> seq;
    seq.push_back({proto::frametype::kKeyDown, kHidLCtrl, kModLeftCtrl, 0, 0, 0, false});
    seq.push_back({proto::frametype::kKeyDown, kHidC, kModLeftCtrl, 0, 0, 0, false});
    seq.push_back({proto::frametype::kKeyUp, kHidC, kModLeftCtrl, 0, 0, 0, false});
    seq.push_back({proto::frametype::kKeyUp, kHidLCtrl, 0, 0, 0, 0, false});
    return seq;
}

// 一次捕获事件 → 一个线上帧（经协议层编码）。
//
// 编码走 fg_input_encode → protocol.EncodeInput：重映射、载荷拼装全在协议层。
QByteArray encodeEvent(const RawEvent &event, std::uint8_t peerPlatform,
                       std::uint8_t *outFrameType)
{
    std::uint16_t outKey = 0;
    std::uint8_t outModifiers = 0;
    const proto::GoBuffer payload =
        proto::encodeInput(event.kind, event.key, event.modifiers, event.deltaX, event.deltaY,
                            event.button, event.pressed, proto::platform::kLinux, peerPlatform,
                            nullptr, QString(), outFrameType, &outKey, &outModifiers);
    return payload.toByteArray();
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    // ── Linux 控制端 → macOS 被控端：Ctrl+C 应当变成 Cmd+C ─────────────
    {
        input::PeerSession target;
        input::PeerSession controller;
        RecordingDecoder targetDecoder;

        target.setLocalIdentity(QStringLiteral("mac-mini"), proto::platform::kMacOS);
        controller.setLocalIdentity(QStringLiteral("deepin-pc"), proto::platform::kLinux);
        // 两端配对码一致 → 会话 HMAC 密钥一致 → 帧才解得开。
        target.setPairingCode(QStringLiteral("4821"));
        controller.setPairingCode(QStringLiteral("4821"));

        QObject::connect(&target, &input::PeerSession::frameReceived, &app,
                         [&targetDecoder](std::uint8_t type, const QByteArray &payload) {
                             targetDecoder.handle(type, payload);
                         });
        QObject::connect(&target, &input::PeerSession::inviteReceived, &target,
                         [&target](const QString &, const QString &) {
                             target.respondInvite(true);
                         });

        bool replayed = false;
        QObject::connect(&controller, &input::PeerSession::stateChanged, &controller,
                         [&](input::PeerSession::State state, const QString &) {
                             if (state != input::PeerSession::State::Connected || replayed) {
                                 return;
                             }
                             replayed = true;
                             // 握手一通，立刻把「用户按住 Ctrl 点了一下 C」重放出去。
                             for (const RawEvent &event : ctrlCOnLinux()) {
                                 std::uint8_t frameType = 0;
                                 const QByteArray payload =
                                     encodeEvent(event, proto::platform::kMacOS, &frameType);
                                 controller.sendFrame(frameType, payload);
                             }
                         });

        const quint16 port = 34911;
        check(target.listen(port), "被控端监听成功");
        controller.connectTo(QStringLiteral("127.0.0.1"), port);

        QTimer::singleShot(5000, &app, [&app]() { app.exit(3); });
        app.exec();

        // 对端应该收到的是「macOS 上按了 Cmd+C」：
        //   Meta down(带 Meta 掩码) → C down(带 Meta 掩码) → C up → Meta up
        check(targetDecoder.keys.size() == 4, "对端收到 4 个按键动作");
        if (targetDecoder.keys.size() == 4) {
            const auto &k = targetDecoder.keys;
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
        input::PeerSession target;
        input::PeerSession controller;
        RecordingDecoder targetDecoder;

        target.setLocalIdentity(QStringLiteral("linux-b"), proto::platform::kLinux);
        controller.setLocalIdentity(QStringLiteral("linux-a"), proto::platform::kLinux);
        target.setPairingCode(QStringLiteral("4821"));
        controller.setPairingCode(QStringLiteral("4821"));

        QObject::connect(&target, &input::PeerSession::frameReceived, &app,
                         [&targetDecoder](std::uint8_t type, const QByteArray &payload) {
                             targetDecoder.handle(type, payload);
                         });
        QObject::connect(&target, &input::PeerSession::inviteReceived, &target,
                         [&target](const QString &, const QString &) {
                             target.respondInvite(true);
                         });

        bool replayed = false;
        QObject::connect(&controller, &input::PeerSession::stateChanged, &controller,
                         [&](input::PeerSession::State state, const QString &) {
                             if (state != input::PeerSession::State::Connected || replayed) {
                                 return;
                             }
                             replayed = true;
                             for (const RawEvent &event : ctrlCOnLinux()) {
                                 std::uint8_t frameType = 0;
                                 const QByteArray payload =
                                     encodeEvent(event, proto::platform::kLinux, &frameType);
                                 controller.sendFrame(frameType, payload);
                             }
                         });

        const quint16 port = 34912;
        check(target.listen(port), "被控端监听成功");
        controller.connectTo(QStringLiteral("127.0.0.1"), port);

        QTimer::singleShot(5000, &app, [&app]() { app.exit(3); });
        app.exec();

        check(targetDecoder.keys.size() == 4, "对端收到 4 个按键动作");
        if (targetDecoder.keys.size() == 4) {
            const auto &k = targetDecoder.keys;
            check(k[0].hid == kHidLCtrl && k[0].down, "同平台：按下 Ctrl（不换算）");
            check(k[0].mods == kModLeftCtrl, "同平台：掩码仍是 Ctrl");
            check(k[1].hid == kHidC && k[1].mods == kModLeftCtrl, "同平台：C 带 Ctrl 掩码");
            check(k[3].hid == kHidLCtrl && !k[3].down, "同平台：松开 Ctrl");
        }
    }

    // ── 鼠标：移动 + 按下 + 抬起 + 滚轮 ────────────────────────────────
    {
        input::PeerSession target;
        input::PeerSession controller;
        RecordingDecoder targetDecoder;

        target.setLocalIdentity(QStringLiteral("mac-mini"), proto::platform::kMacOS);
        controller.setLocalIdentity(QStringLiteral("deepin-pc"), proto::platform::kLinux);
        target.setPairingCode(QStringLiteral("4821"));
        controller.setPairingCode(QStringLiteral("4821"));

        QObject::connect(&target, &input::PeerSession::frameReceived, &app,
                         [&targetDecoder](std::uint8_t type, const QByteArray &payload) {
                             targetDecoder.handle(type, payload);
                         });
        QObject::connect(&target, &input::PeerSession::inviteReceived, &target,
                         [&target](const QString &, const QString &) {
                             target.respondInvite(true);
                         });

        bool replayed = false;
        QObject::connect(&controller, &input::PeerSession::stateChanged, &controller,
                         [&](input::PeerSession::State state, const QString &) {
                             if (state != input::PeerSession::State::Connected || replayed) {
                                 return;
                             }
                             replayed = true;

                             const std::vector<RawEvent> mouseEvents = {
                                 {proto::frametype::kMouseMove, 0, 0, 12, -7, 0, false},
                                 {proto::frametype::kMouseButton, 0, 0, 0, 0, 0x01, true},
                                 {proto::frametype::kMouseButton, 0, 0, 0, 0, 0x01, false},
                                 {proto::frametype::kScroll, 0, 0, 0, 3, 0, false},
                             };
                             for (const RawEvent &event : mouseEvents) {
                                 std::uint8_t frameType = 0;
                                 const QByteArray payload =
                                     encodeEvent(event, proto::platform::kMacOS, &frameType);
                                 controller.sendFrame(frameType, payload);
                             }
                         });

        const quint16 port = 34913;
        check(target.listen(port), "被控端监听成功");
        controller.connectTo(QStringLiteral("127.0.0.1"), port);

        QTimer::singleShot(5000, &app, [&app]() { app.exit(3); });
        app.exec();

        check(targetDecoder.moves.size() == 1, "对端收到一次鼠标移动");
        if (targetDecoder.moves.size() == 1) {
            check(targetDecoder.moves.front().first == 12, "移动 dx 正确");
            check(targetDecoder.moves.front().second == -7, "移动 dy 正确");
        }
        check(targetDecoder.buttons.size() == 2, "对端收到两次鼠标按钮（按下 + 抬起）");
        if (targetDecoder.buttons.size() == 2) {
            check(targetDecoder.buttons[0].second, "第 1 次是按下");
            check(!targetDecoder.buttons[1].second, "第 2 次是抬起");
            check(targetDecoder.buttons[0].first == 0x01, "按钮号正确");
        }
        check(targetDecoder.scrolls.size() == 1, "对端收到一次滚轮");
        if (targetDecoder.scrolls.size() == 1) {
            check(targetDecoder.scrolls.front().first == 3, "滚轮 deltaY 正确");
        }
    }

    if (failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("input_protocol_e2e_test: OK\n");
    return EXIT_SUCCESS;
}
