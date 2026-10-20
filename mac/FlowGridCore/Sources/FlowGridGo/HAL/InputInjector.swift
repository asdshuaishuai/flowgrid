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

    /// 注入一帧 HID 载荷；返回是否处理。
    @discardableResult
    func inject(frameType: UInt8, payload: Data) -> Bool {
        switch frameType {
        case FGFrameType.keyDown:
            guard let parsed = GoPayloads.parseKeyDown(payload),
                  let keyCode = KeyCodeTable.virtualKey(forHID: parsed.keyCode)
            else { return false }
            postKey(keyCode: keyCode, isDown: true, modifiers: parsed.modifiers)
            return true

        case FGFrameType.keyUp:
            guard let usage = GoPayloads.parseKeyUp(payload),
                  let keyCode = KeyCodeTable.virtualKey(forHID: usage)
            else { return false }
            postKey(keyCode: keyCode, isDown: false, modifiers: 0)
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

    // MARK: - 键盘

    private func postKey(keyCode: CGKeyCode, isDown: Bool, modifiers: UInt8) {
        guard let event = CGEvent(keyboardEventSource: nil, virtualKey: keyCode, keyDown: isDown) else { return }
        event.flags = KeyCodeTable.flags(fromModifiers: modifiers)
        event.post(tap: .cghidEventTap)
    }

    // MARK: - 鼠标

    private func currentMouseLocation() -> CGPoint {
        if let mouseLocation { return mouseLocation }
        let location = CGEvent(source: nil)?.location ?? .zero
        mouseLocation = location
        return location
    }

    private func postMouseMove(dx: Int16, dy: Int16) {
        let current = currentMouseLocation()
        let target = CGPoint(x: current.x + CGFloat(dx), y: current.y + CGFloat(dy))
        mouseLocation = target
        let type: CGEventType = buttonDown.isEmpty ? .mouseMoved : .leftMouseDragged
        guard let event = CGEvent(
            mouseEventSource: nil, mouseType: type,
            mouseCursorPosition: target, mouseButton: .left
        ) else { return }
        event.post(tap: .cghidEventTap)
    }

    private func postMouseButton(button: UInt8, isDown: Bool) {
        let cgButton: CGMouseButton
        let downType: CGEventType
        let upType: CGEventType
        switch button {
        case 0x01: (cgButton, downType, upType) = (.left, .leftMouseDown, .leftMouseUp)
        case 0x02: (cgButton, downType, upType) = (.right, .rightMouseDown, .rightMouseUp)
        case 0x03: (cgButton, downType, upType) = (.center, .otherMouseDown, .otherMouseUp)
        default: return
        }
        if isDown {
            buttonDown.insert(button)
        } else {
            buttonDown.remove(button)
        }
        let location = currentMouseLocation()
        guard let event = CGEvent(
            mouseEventSource: nil,
            mouseType: isDown ? downType : upType,
            mouseCursorPosition: location,
            mouseButton: cgButton
        ) else { return }
        event.post(tap: .cghidEventTap)
    }

    private func postScroll(dy: Int16, dx: Int16) {
        guard let event = CGEvent(
            scrollWheelEvent2Source: nil,
            units: .pixel,
            wheelCount: 2,
            wheel1: Int32(dy),
            wheel2: Int32(dx),
            wheel3: 0
        ) else { return }
        event.post(tap: .cghidEventTap)
    }
}
