#include "input_forwarder.h"

#include <utility>

namespace fg::input {

Forwarder::Forwarder(Injector *injector, SendFn send)
    : injector_(injector), send_(std::move(send))
{
}

bool Forwarder::forwardCaptured(const CapturedEvent &event)
{
    // 「事件 → 帧类型 + 载荷」整件事交给协议层：客户端只做「OS 原生事件 ↔
    // 协议模型」的翻译，不自己拼载荷、不自己做跨平台换算。
    //
    // 为什么这么收：这段语义（哪种操作用哪种帧、载荷怎么拼、Ctrl⇄Meta 怎么换）
    // 以前在 deepin 与 mac 各写一份，两端各写各的期望值就会分叉——协议层的
    // 黄金向量（protocol/testdata/input_vectors.json）钉的就是这段。
    //
    // kind 直接用帧类型常量：协议层里操作类型与帧类型一一对应且同值。
    std::uint8_t kind = proto::frametype::kKeyDown;
    std::uint16_t key = 0;
    std::uint8_t modifiers = 0;
    std::int16_t deltaX = 0;
    std::int16_t deltaY = 0;
    std::uint8_t button = 0;
    bool pressed = false;
    switch (event.kind) {
    case EventKind::KeyDown:
        kind = proto::frametype::kKeyDown;
        key = event.keyCode;
        modifiers = event.modifiers;
        break;
    case EventKind::KeyUp:
        kind = proto::frametype::kKeyUp;
        key = event.keyCode;
        break;
    case EventKind::MouseMove:
        kind = proto::frametype::kMouseMove;
        deltaX = event.deltaX;
        deltaY = event.deltaY;
        break;
    case EventKind::MouseButton:
        kind = proto::frametype::kMouseButton;
        button = event.button;
        pressed = event.pressed;
        break;
    case EventKind::Scroll:
        kind = proto::frametype::kScroll;
        deltaX = event.deltaX;
        deltaY = event.deltaY;
        break;
    }

    std::uint8_t frameType = 0;
    std::uint16_t remappedKey = 0;
    std::uint8_t remappedMods = 0;
    const proto::GoBuffer payload = proto::encodeInput(kind, key, modifiers, deltaX, deltaY,
                                                        button, pressed,
                                                        proto::platform::kLinux, peerPlatform_,
                                                        keymap_, context_, &frameType,
                                                        &remappedKey, &remappedMods);
    if (payload.isNull()) {
        lastError_ = QStringLiteral("协议层编码失败");
        return false;
    }

    OutboundFrame frame;
    frame.frameType = frameType;
    frame.payload = payload.toByteArray();

    // 记住的是**重映射后**的键位/按钮：停接管时要按同一个键码发 keyUp，
    // 否则对端会多出一个按下的键（对端在另一台机器上，键会一直卡着）。
    if (event.kind == EventKind::KeyDown) {
        pressedKeys_.insert(remappedKey);
    } else if (event.kind == EventKind::KeyUp) {
        pressedKeys_.erase(remappedKey);
    } else if (event.kind == EventKind::MouseButton) {
        if (event.pressed) {
            pressedButtons_.insert(event.button);
        } else {
            pressedButtons_.erase(event.button);
        }
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
