// 键鼠转发通路的单测：重映射、组帧、注入分发。
//
// 不需要 /dev/uinput、不需要真键盘：注入侧用假 Injector，发送侧用假回调。
// 这正是把 Injector 抽成接口的目的——真机才有的权限不该是单测的前提。
//
// 构建运行（见 deepin/CMakeLists.txt 的 flowgrid-inputforwardtest 目标）：
//   cmake --build deepin/build-dev --target flowgrid-inputforwardtest
//   ./deepin/build-dev/flowgrid-inputforwardtest
#include "core/input/input_forwarder.h"

#include <cstdio>
#include <cstdlib>
#include <string>
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

// 假注入目标：只记录收到的注入动作。
class FakeInjector : public Injector {
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

struct SentFrame {
    std::uint8_t type;
    QByteArray payload;
};

// 协议 §6.2 修饰位（与 protocol/keymap.go 同值）。
constexpr std::uint8_t kModLeftCtrl = 0x01;
constexpr std::uint8_t kModLeftMeta = 0x08;

// HID Usage（§6.1）：A / 左 Ctrl / 左 Meta。
constexpr std::uint16_t kHidA = 0x04;
constexpr std::uint16_t kHidLCtrl = 0xE0;
constexpr std::uint16_t kHidLMeta = 0xE3;

} // namespace

int main()
{
    using namespace fg;
    using namespace fg::input;

    // ── ① 发送侧重映射：Linux → macOS ─────────────────────────────────
    // 平台默认的 Ctrl⇄Meta 换算必须由协议层做，客户端只调入口。
    {
        std::uint16_t key = 0;
        std::uint8_t mods = 0;
        const bool ok = proto::remapCapturedKey(kHidLCtrl, kModLeftCtrl,
                                                proto::platform::kLinux,
                                                proto::platform::kMacOS,
                                                nullptr, QString(),
                                                &key, &mods);
        check(ok, "remapCapturedKey 成功");
        check(key == kHidLMeta, "Linux Ctrl → macOS Meta（键码）");
        check(mods == kModLeftMeta, "Linux Ctrl → macOS Meta（掩码）");
    }

    // 同平台：原样返回，不该有任何换算。
    {
        std::uint16_t key = 0;
        std::uint8_t mods = 0;
        const bool ok = proto::remapCapturedKey(kHidLCtrl, kModLeftCtrl,
                                                proto::platform::kLinux,
                                                proto::platform::kLinux,
                                                nullptr, QString(),
                                                &key, &mods);
        check(ok, "同平台重映射成功");
        check(key == kHidLCtrl, "同平台键码不变");
        check(mods == kModLeftCtrl, "同平台掩码不变");
    }

    // 非修饰键：跨平台也不该变（只有修饰键换算）。
    {
        std::uint16_t key = 0;
        std::uint8_t mods = 0;
        const bool ok = proto::remapCapturedKey(kHidA, 0,
                                                proto::platform::kLinux,
                                                proto::platform::kMacOS,
                                                nullptr, QString(),
                                                &key, &mods);
        check(ok, "普通键重映射成功");
        check(key == kHidA, "普通键跨平台不变");
    }

    // ── ② 控制端：捕获事件 → 组帧 ─────────────────────────────────────
    std::vector<SentFrame> sent;
    FakeInjector injector;
    Forwarder forwarder(&injector, [&sent](const OutboundFrame &frame) {
        sent.push_back(SentFrame{frame.frameType, frame.payload});
    });
    forwarder.setPeerPlatform(proto::platform::kMacOS);

    {
        CapturedEvent event;
        event.kind = EventKind::KeyDown;
        event.keyCode = kHidA;
        event.modifiers = kModLeftCtrl;
        check(forwarder.forwardCaptured(event), "KEY_DOWN 转发成功");
        check(sent.size() == 1, "发出一条帧");
        check(sent.back().type == proto::frametype::kKeyDown, "帧类型是 KEY_DOWN");

        proto::KeyDownPayload parsed;
        check(proto::parseKeyDown(sent.back().payload, &parsed), "KEY_DOWN 载荷可解析");
        check(parsed.keyCode == kHidA, "普通键码原样");
        check(parsed.modifiers == kModLeftMeta, "Ctrl 掩码换成 Meta");
    }

    // KEY_UP 不带掩码，但**键码仍要重映射**——对端是 macOS 时，松开的
    // 必须还是 Meta，否则对端的 Cmd 会一直按着。
    {
        sent.clear();
        CapturedEvent event;
        event.kind = EventKind::KeyUp;
        event.keyCode = kHidLCtrl;
        check(forwarder.forwardCaptured(event), "KEY_UP 转发成功");
        proto::KeyUpPayload parsed;
        check(proto::parseKeyUp(sent.back().payload, &parsed), "KEY_UP 载荷可解析");
        check(parsed.keyCode == kHidLMeta, "KEY_UP 的键码也走了重映射");
    }

    // 鼠标按钮：按下与松开必须能被对端区分开。
    {
        sent.clear();
        CapturedEvent down;
        down.kind = EventKind::MouseButton;
        down.button = 0x01;
        down.pressed = true;
        check(forwarder.forwardCaptured(down), "鼠标按下转发成功");

        CapturedEvent up;
        up.kind = EventKind::MouseButton;
        up.button = 0x01;
        up.pressed = false;
        check(forwarder.forwardCaptured(up), "鼠标松开转发成功");
        check(sent.size() == 2, "发出两条鼠标按钮帧");

        proto::MouseButtonPayload first;
        proto::MouseButtonPayload second;
        check(proto::parseMouseButton(sent[0].payload, &first), "按下载荷可解析");
        check(proto::parseMouseButton(sent[1].payload, &second), "松开载荷可解析");
        check(first.buttonId == 0x01 && first.down, "按下是 state=1");
        check(second.buttonId == 0x01 && !second.down, "松开是 state=0");
    }

    // 滚轮：deltaY/deltaX 的位置不能反（协议正=向左）。
    {
        sent.clear();
        CapturedEvent event;
        event.kind = EventKind::Scroll;
        event.deltaY = 3;
        event.deltaX = -2;
        check(forwarder.forwardCaptured(event), "滚轮转发成功");
        proto::ScrollPayload parsed;
        check(proto::parseScroll(sent.back().payload, &parsed), "滚轮载荷可解析");
        check(parsed.deltaY == 3, "滚轮 deltaY");
        check(parsed.deltaX == -2, "滚轮 deltaX");
    }

    // 鼠标移动：不重映射，原样转发。
    {
        sent.clear();
        CapturedEvent event;
        event.kind = EventKind::MouseMove;
        event.deltaX = 12;
        event.deltaY = -7;
        check(forwarder.forwardCaptured(event), "鼠标移动转发成功");
        proto::MouseMovePayload parsed;
        check(proto::parseMouseMove(sent.back().payload, &parsed), "移动载荷可解析");
        check(parsed.deltaX == 12 && parsed.deltaY == -7, "移动增量原样");
    }

    // ── ③ 接收端：入站帧 → 注入 ───────────────────────────────────────
    {
        FakeInjector target;
        Forwarder receiver(&target, nullptr);
        const QByteArray payload = proto::keyDownPayload(kHidA, kModLeftMeta).toByteArray();
        check(receiver.handleInbound(proto::frametype::kKeyDown, payload), "入站 KEY_DOWN 处理成功");
        check(target.keys.size() == 1, "注入了一次按键");
        check(target.keys.front().hid == kHidA, "注入的键码正确");
        check(target.keys.front().down, "注入的是按下");
        check(target.keys.front().mods == kModLeftMeta, "注入的掩码正确");
    }

    {
        FakeInjector target;
        Forwarder receiver(&target, nullptr);
        const QByteArray payload =
            proto::mouseButtonPayload(0x01, true).toByteArray();
        check(receiver.handleInbound(proto::frametype::kMouseButton, payload),
              "入站 MOUSE_BUTTON 处理成功");
        check(target.buttons.size() == 1, "注入了一次鼠标按钮");
        check(target.buttons.front().second, "注入的是按下");
    }

    // 非输入帧：不该被当成输入注入（剪贴板/心跳也走同一个入口）。
    {
        FakeInjector target;
        Forwarder receiver(&target, nullptr);
        check(!receiver.handleInbound(proto::frametype::kClipboard, QByteArray("x")),
              "非输入帧不被当作输入处理");
        check(target.keys.empty() && target.buttons.empty(), "非输入帧没有触发注入");
    }

    // 坏载荷：如实失败，不注入。
    {
        FakeInjector target;
        Forwarder receiver(&target, nullptr);
        check(!receiver.handleInbound(proto::frametype::kKeyDown, QByteArray()),
              "坏载荷被拒");
        check(target.keys.empty(), "坏载荷没有触发注入");
    }

    if (failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("input_forwarder_test: OK\n");
    return EXIT_SUCCESS;
}
