#include "input_forwarder.h"

#include <utility>

namespace fg::input {

Forwarder::Forwarder(Injector *injector, SendFn send)
    : injector_(injector), send_(std::move(send))
{
}

bool Forwarder::forwardCaptured(const CapturedEvent &event)
{
    OutboundFrame frame;
    switch (event.kind) {
    case EventKind::KeyDown: {
        std::uint16_t key = event.keyCode;
        std::uint8_t mods = event.modifiers;
        if (!proto::remapCapturedKey(event.keyCode, event.modifiers,
                                     proto::platform::kLinux, peerPlatform_,
                                     keymap_, context_, &key, &mods)) {
            lastError_ = QStringLiteral("按键重映射失败（HID 0x%1）")
                             .arg(event.keyCode, 4, 16, QLatin1Char('0'));
            return false;
        }
        frame.frameType = proto::frametype::kKeyDown;
        frame.payload = proto::keyDownPayload(key, mods).toByteArray();
        pressedKeys_.insert(key);
        break;
    }
    case EventKind::KeyUp: {
        // KEY_UP 不带修饰掩码（§5.3），但**键码仍然要重映射**：掩码没了，
        // 键位换算照样得按目标平台走。对端是 macOS 时，松开 Ctrl 必须发
        // Meta 的 keyUp，否则对端的 Cmd 会一直按着。
        std::uint16_t key = event.keyCode;
        std::uint8_t mods = 0;
        if (!proto::remapCapturedKey(event.keyCode, 0, proto::platform::kLinux,
                                     peerPlatform_, keymap_, context_, &key, &mods)) {
            lastError_ = QStringLiteral("按键释放重映射失败（HID 0x%1）")
                             .arg(event.keyCode, 4, 16, QLatin1Char('0'));
            return false;
        }
        frame.frameType = proto::frametype::kKeyUp;
        frame.payload = proto::keyUpPayload(key).toByteArray();
        pressedKeys_.erase(key);
        break;
    }
    case EventKind::MouseMove:
        frame.frameType = proto::frametype::kMouseMove;
        frame.payload = proto::mouseMovePayload(event.deltaX, event.deltaY).toByteArray();
        break;
    case EventKind::MouseButton:
        frame.frameType = proto::frametype::kMouseButton;
        frame.payload =
            proto::mouseButtonPayload(event.button, event.pressed).toByteArray();
        if (event.pressed) {
            pressedButtons_.insert(event.button);
        } else {
            pressedButtons_.erase(event.button);
        }
        break;
    case EventKind::Scroll:
        frame.frameType = proto::frametype::kScroll;
        frame.payload = proto::scrollPayload(event.deltaY, event.deltaX).toByteArray();
        break;
    }

    if (!send_) {
        lastError_ = QStringLiteral("没有可用的发送通道");
        return false;
    }
    send_(frame);
    return true;
}

void Forwarder::releasePressed()
{
    if (!send_) {
        pressedKeys_.clear();
        pressedButtons_.clear();
        return;
    }
    // 松开用**按下时记下的那个键码**，不重新算：重映射的输入（掩码）不同，
    // 算出来的键码可能对不上，对端就会多出一个按下的键。
    for (std::uint16_t key : pressedKeys_) {
        OutboundFrame frame;
        frame.frameType = proto::frametype::kKeyUp;
        frame.payload = proto::keyUpPayload(key).toByteArray();
        send_(frame);
    }
    for (std::uint8_t button : pressedButtons_) {
        OutboundFrame frame;
        frame.frameType = proto::frametype::kMouseButton;
        frame.payload = proto::mouseButtonPayload(button, false).toByteArray();
        send_(frame);
    }
    pressedKeys_.clear();
    pressedButtons_.clear();
}

bool Forwarder::handleInbound(std::uint8_t frameType, const QByteArray &payload)
{
    if (injector_ == nullptr) {
        lastError_ = QStringLiteral("没有注入目标");
        return false;
    }
    switch (frameType) {
    case proto::frametype::kKeyDown: {
        proto::KeyDownPayload parsed;
        if (!proto::parseKeyDown(payload, &parsed)) {
            lastError_ = QStringLiteral("KEY_DOWN 载荷解析失败");
            return false;
        }
        return injector_->injectKey(parsed.keyCode, true, parsed.modifiers);
    }
    case proto::frametype::kKeyUp: {
        proto::KeyUpPayload parsed;
        if (!proto::parseKeyUp(payload, &parsed)) {
            lastError_ = QStringLiteral("KEY_UP 载荷解析失败");
            return false;
        }
        return injector_->injectKey(parsed.keyCode, false, 0);
    }
    case proto::frametype::kMouseMove: {
        proto::MouseMovePayload parsed;
        if (!proto::parseMouseMove(payload, &parsed)) {
            lastError_ = QStringLiteral("MOUSE_MOVE 载荷解析失败");
            return false;
        }
        return injector_->injectMouseMove(parsed.deltaX, parsed.deltaY);
    }
    case proto::frametype::kMouseButton: {
        proto::MouseButtonPayload parsed;
        if (!proto::parseMouseButton(payload, &parsed)) {
            lastError_ = QStringLiteral("MOUSE_BUTTON 载荷解析失败");
            return false;
        }
        return injector_->injectMouseButton(parsed.buttonId, parsed.down);
    }
    case proto::frametype::kScroll: {
        proto::ScrollPayload parsed;
        if (!proto::parseScroll(payload, &parsed)) {
            lastError_ = QStringLiteral("SCROLL 载荷解析失败");
            return false;
        }
        return injector_->injectScroll(parsed.deltaY, parsed.deltaX);
    }
    default:
        // 不是输入帧：不是错误——剪贴板、文件、心跳等帧也走这条入口。
        lastError_ = QStringLiteral("不是输入帧（0x%1）")
                         .arg(frameType, 2, 16, QLatin1Char('0'));
        return false;
    }
}

} // namespace fg::input
