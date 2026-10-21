import AppKit
import ApplicationServices
import CoreGraphics
import FlowGridCore
import Foundation

/// 把协议 HID 事件注入本机（目标端）。需要辅助功能权限（AXIsProcessTrusted）。
@MainActor
final class InputInjector {

    private var mouseLocation: CGPoint?
    private var buttonDown: Set<UInt8> = []
    /// 已注入且尚未收到 KEY_UP 的虚拟键码。断线时逐个补 keyUp：
    /// 注入事件不会因对端消失而自动收尾。
    private var keysDown: Set<CGKeyCode> = []
    /// 最近一帧 KEY_DOWN 携带的修饰位。KEY_UP 载荷没有修饰位，若按 0 发，
    /// 会让目标应用看到「松开按键时所有修饰键都抬起了」，Shift/Ctrl 组合
    /// 键的 keyUp 语义会错。这里维护一份会话内状态，keyUp 沿用当前值。
    private var modifiers: UInt8 = 0

    /// 注入一帧 HID 载荷；返回是否处理。
    @discardableResult
    func inject(frameType: UInt8, payload: Data) -> Bool {
        switch frameType {
        case FGFrameType.keyDown:
            guard let parsed = GoPayloads.parseKeyDown(payload),
                  let keyCode = KeyCodeTable.virtualKey(forHID: parsed.keyCode)
            else { return false }
            modifiers = parsed.modifiers
            // 发送端的掩码正常情况下已包含这个修饰键；万一漏了，注入侧补上，
            // 保证随后的 keyUp 能把状态清干净。
            if let bit = KeyCodeTable.modifierBit(forHID: parsed.keyCode) {
                modifiers |= bit
            }
            postKey(keyCode: keyCode, isDown: true, modifiers: modifiers)
            return true

        case FGFrameType.keyUp:
            guard let usage = GoPayloads.parseKeyUp(payload),
                  let keyCode = KeyCodeTable.virtualKey(forHID: usage)
            else { return false }
            postKey(keyCode: keyCode, isDown: false, modifiers: modifiers)
            if let bit = KeyCodeTable.modifierBit(forHID: usage) {
                modifiers &= ~bit
            }
            return true

        case FGFrameType.mouseMove:
            guard let (dx, dy) = GoPayloads.parseMouseMove(payload) else { return false }
            postMouseMove(dx: dx, dy: dy)
            return true

        case FGFrameType.mouseButton:
            guard let (button, state) = GoPayloads.parseMouseButton(payload) else { return false }
            postMouseButton(button: button, isDown: state == 1)
            return true

        case FGFrameType.scroll:
            guard let (dy, dx) = GoPayloads.parseScroll(payload) else { return false }
            postScroll(dy: dy, dx: dx)
            return true

        default:
            return false
        }
    }

    /// 会话结束/断线时松开所有仍按着的鼠标键与修饰键。
    /// 注入的是 CGEvent 而不是真实 HID 设备，拔线不会让系统自动清状态；
    /// 不主动收尾的话，对端在按住 Ctrl 时断线，本机会一直以为 Ctrl 还按着。
    func reset() {
        for button in Array(buttonDown) {
            postMouseButton(button: button, isDown: false)
        }
        for keyCode in Array(keysDown) {
            postKey(keyCode: keyCode, isDown: false, modifiers: modifiers)
        }
        keysDown.removeAll()
        modifiers = 0
        mouseLocation = nil
    }

    // MARK: - 键盘

    private func postKey(keyCode: CGKeyCode, isDown: Bool, modifiers: UInt8) {
        guard let event = CGEvent(keyboardEventSource: nil, virtualKey: keyCode, keyDown: isDown) else { return }
        var flagsMask = modifiers
        if !isDown, let usage = KeyCodeTable.hidUsage(forVirtualKey: keyCode),
           let bit = KeyCodeTable.modifierBit(forHID: usage) {
            // 松开修饰键的那一帧必须把该位从 flags 里去掉，否则事件自身还在
            // 宣称「这个修饰键仍按着」，系统可能不认这次 keyUp。
            flagsMask &= ~bit
        }
        event.flags = KeyCodeTable.flags(fromModifiers: flagsMask)
        event.setIntegerValueField(.eventSourceUserData, value: InputMarker.injected)
        event.post(tap: .cghidEventTap)
        if isDown {
            keysDown.insert(keyCode)
        } else {
            keysDown.remove(keyCode)
        }
    }

    // MARK: - 鼠标

    private func currentMouseLocation() -> CGPoint {
        if let mouseLocation { return mouseLocation }
        let location = CGEvent(source: nil)?.location ?? .zero
        mouseLocation = location
        return location
    }

    private func postMouseMove(dx: Int16, dy: Int16) {
        guard dx != 0 || dy != 0 else { return }
        let current = currentMouseLocation()
        let target = CGPoint(x: current.x + CGFloat(dx), y: current.y + CGFloat(dy))
        mouseLocation = target
        let (type, button, buttonNumber) = dragState()
        guard let event = CGEvent(
            mouseEventSource: nil, mouseType: type,
            mouseCursorPosition: target, mouseButton: button
        ) else { return }
        if let buttonNumber {
            // CGEvent 只给 left/right/center 三个常量；侧键靠这个字段区分。
            event.setIntegerValueField(.mouseEventButtonNumber, value: buttonNumber)
        }
        event.flags = KeyCodeTable.flags(fromModifiers: modifiers)
        event.setIntegerValueField(.eventSourceUserData, value: InputMarker.injected)
        event.post(tap: .cghidEventTap)
    }

    /// 拖拽事件类型必须跟按下的键一致：右键拖拽发 leftMouseDragged 会让
    /// 应用收到错误的事件类型（右键菜单/框选行为都不对）。返回值第三项是
    /// 侧键编号（写入 mouseEventButtonNumber），主/中键为 nil。
    private func dragState() -> (CGEventType, CGMouseButton, Int64?) {
        if buttonDown.contains(0x01) { return (.leftMouseDragged, .left, nil) }
        if buttonDown.contains(0x02) { return (.rightMouseDragged, .right, nil) }
        if buttonDown.contains(0x03) { return (.otherMouseDragged, .center, 2) }
        if buttonDown.contains(0x04) { return (.otherMouseDragged, .center, 3) }
        if buttonDown.contains(0x05) { return (.otherMouseDragged, .center, 4) }
        return (.mouseMoved, .left, nil)
    }

    private func postMouseButton(button: UInt8, isDown: Bool) {
        let cgButton: CGMouseButton
        let downType: CGEventType
        let upType: CGEventType
        switch button {
        case 0x01: (cgButton, downType, upType) = (.left, .leftMouseDown, .leftMouseUp)
        case 0x02: (cgButton, downType, upType) = (.right, .rightMouseDown, .rightMouseUp)
        case 0x03, 0x04, 0x05: (cgButton, downType, upType) = (.center, .otherMouseDown, .otherMouseUp)
        default: return
        }
        let location = currentMouseLocation()
        guard let event = CGEvent(
            mouseEventSource: nil,
            mouseType: isDown ? downType : upType,
            mouseCursorPosition: location,
            mouseButton: cgButton
        ) else { return }
        if button >= 0x04 {
            event.setIntegerValueField(.mouseEventButtonNumber, value: Int64(button) - 1)
        }
        if isDown {
            buttonDown.insert(button)
        } else {
            buttonDown.remove(button)
        }
        event.flags = KeyCodeTable.flags(fromModifiers: modifiers)
        event.setIntegerValueField(.eventSourceUserData, value: InputMarker.injected)
        event.post(tap: .cghidEventTap)
    }

    private func postScroll(dy: Int16, dx: Int16) {
        guard dy != 0 || dx != 0 else { return }
        // 捕获侧取的是 scrollWheelEventDeltaAxis（行/格），这里必须用 .line。
        // 之前用 .pixel，一格滚轮只滚 1 像素，几乎等于没反应。
        guard let event = CGEvent(
            scrollWheelEvent2Source: nil,
            units: .line,
            wheelCount: 2,
            wheel1: Int32(dy),
            wheel2: Int32(dx),
            wheel3: 0
        ) else { return }
        event.flags = KeyCodeTable.flags(fromModifiers: modifiers)
        event.setIntegerValueField(.eventSourceUserData, value: InputMarker.injected)
        event.post(tap: .cghidEventTap)
    }
}
