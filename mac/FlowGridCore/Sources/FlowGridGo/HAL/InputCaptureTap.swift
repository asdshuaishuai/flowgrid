import AppKit
import ApplicationServices
import CoreGraphics
import FlowGridCore
import Foundation

/// 从 CGEvent 提炼的可跨隔离样本（全部 Sendable 标量）。
struct CapturedEvent: Sendable {
    enum Kind: Sendable {
        case keyDown
        case keyUp
        case flagsChanged
        case mouseMove
        case mouseButton(button: UInt8, isDown: Bool)
        case scroll
    }

    let kind: Kind
    let virtualKey: Int64
    let flagsRaw: UInt64
    let deltaX: Int64
    let deltaY: Int64
}

/// 本机键鼠捕获（控制端）：CGEventTap 拦截并吞掉事件，转成协议 HID 载荷发出。
/// 需要辅助功能权限（AXIsProcessTrusted）；仅应在已连接且用户显式开启时启用。
@MainActor
final class InputCaptureTap {

    private var tap: CFMachPort?
    private var runLoopSource: CFRunLoopSource?
    private var send: ((UInt8, Data) -> Void)?

    static var isTrusted: Bool {
        AXIsProcessTrusted()
    }

    func start(send: @escaping (UInt8, Data) -> Void) -> Bool {
        guard tap == nil else { return true }
        guard AXIsProcessTrusted() else { return false }

        let mask: CGEventMask =
            (1 << CGEventType.keyDown.rawValue)
            | (1 << CGEventType.keyUp.rawValue)
            | (1 << CGEventType.flagsChanged.rawValue)
            | (1 << CGEventType.mouseMoved.rawValue)
            | (1 << CGEventType.leftMouseDragged.rawValue)
            | (1 << CGEventType.rightMouseDragged.rawValue)
            | (1 << CGEventType.otherMouseDragged.rawValue)
            | (1 << CGEventType.leftMouseDown.rawValue)
            | (1 << CGEventType.leftMouseUp.rawValue)
            | (1 << CGEventType.rightMouseDown.rawValue)
            | (1 << CGEventType.rightMouseUp.rawValue)
            | (1 << CGEventType.otherMouseDown.rawValue)
            | (1 << CGEventType.otherMouseUp.rawValue)
            | (1 << CGEventType.scrollWheel.rawValue)

        let refcon = Unmanaged.passUnretained(self).toOpaque()
        guard let tap = CGEvent.tapCreate(
            tap: .cgSessionEventTap,
            place: .headInsertEventTap,
            options: .defaultTap,
            eventsOfInterest: mask,
            callback: flowgridInputTapCallback,
            userInfo: refcon
        ) else {
            return false
        }

        self.tap = tap
        self.send = send
        let source = CFMachPortCreateRunLoopSource(kCFAllocatorDefault, tap, 0)
        runLoopSource = source
        CFRunLoopAddSource(CFRunLoopGetMain(), source, .commonModes)
        CGEvent.tapEnable(tap: tap, enable: true)
        return true
    }

    func stop() {
        if let tap {
            CGEvent.tapEnable(tap: tap, enable: false)
            CFMachPortInvalidate(tap)
        }
        if let runLoopSource {
            CFRunLoopRemoveSource(CFRunLoopGetMain(), runLoopSource, .commonModes)
        }
        tap = nil
        runLoopSource = nil
        send = nil
    }

    /// tap 被系统临时禁用（超时/用户输入）后重新启用。
    fileprivate func reenable() {
        if let tap {
            CGEvent.tapEnable(tap: tap, enable: true)
        }
    }

    /// 处理样本；返回 true 表示吞掉（不传给本机）。
    fileprivate func handle(_ sample: CapturedEvent) -> Bool {
        let flags = CGEventFlags(rawValue: sample.flagsRaw)
        switch sample.kind {
        case .keyDown, .keyUp:
            let virtualKey = CGKeyCode(sample.virtualKey)
            guard let usage = KeyCodeTable.hidUsage(forVirtualKey: virtualKey) else {
                return true // 未知键吞掉，避免本机与对端行为分叉
            }
            let modifiers = KeyCodeTable.modifierMask(from: flags)
            if case .keyDown = sample.kind {
                if let payload = try? GoPayloads.keyDown(keyCode: usage, modifiers: modifiers) {
                    send?(FGFrameType.keyDown, payload)
                }
            } else {
                if let payload = try? GoPayloads.keyUp(keyCode: usage) {
                    send?(FGFrameType.keyUp, payload)
                }
            }
            return true

        case .flagsChanged:
            let virtualKey = CGKeyCode(sample.virtualKey)
            guard let usage = KeyCodeTable.hidUsage(forVirtualKey: virtualKey) else {
                return false
            }
            let modifiers = KeyCodeTable.modifierMask(from: flags)
            let pressed: Bool
            switch usage {
            case 0xE0, 0xE4: pressed = flags.contains(.maskControl)
            case 0xE1, 0xE5: pressed = flags.contains(.maskShift)
            case 0xE2, 0xE6: pressed = flags.contains(.maskAlternate)
            case 0xE3, 0xE7: pressed = flags.contains(.maskCommand)
            default: pressed = true
            }
            if pressed {
                if let payload = try? GoPayloads.keyDown(keyCode: usage, modifiers: modifiers) {
                    send?(FGFrameType.keyDown, payload)
                }
            } else {
                if let payload = try? GoPayloads.keyUp(keyCode: usage) {
                    send?(FGFrameType.keyUp, payload)
                }
            }
            return true

        case .mouseMove:
            guard sample.deltaX != 0 || sample.deltaY != 0 else { return true }
            if let payload = try? GoPayloads.mouseMove(
                deltaX: Int16(clamping: sample.deltaX), deltaY: Int16(clamping: sample.deltaY)
            ) {
                send?(FGFrameType.mouseMove, payload)
            }
            return true

        case let .mouseButton(button, isDown):
            if let payload = try? GoPayloads.mouseButton(buttonID: button, state: isDown ? 1 : 0) {
                send?(FGFrameType.mouseButton, payload)
            }
            return true

        case .scroll:
            if let payload = try? GoPayloads.scroll(
                deltaY: Int16(clamping: sample.deltaY), deltaX: Int16(clamping: sample.deltaX)
            ) {
                send?(FGFrameType.scroll, payload)
            }
            return true
        }
    }
}

/// C 回调（文件级函数，无捕获）：只把 Sendable 标量送进主 actor。
private func flowgridInputTapCallback(
    proxy: CGEventTapProxy,
    type: CGEventType,
    event: CGEvent,
    refcon: UnsafeMutableRawPointer?
) -> Unmanaged<CGEvent>? {
    guard let refcon else { return Unmanaged.passUnretained(event) }
    let tap = Unmanaged<InputCaptureTap>.fromOpaque(refcon).takeUnretainedValue()

    let kind: CapturedEvent.Kind
    switch type {
    case .tapDisabledByTimeout, .tapDisabledByUserInput:
        MainActor.assumeIsolated { tap.reenable() }
        return Unmanaged.passUnretained(event)
    case .keyDown:
        kind = .keyDown
    case .keyUp:
        kind = .keyUp
    case .flagsChanged:
        kind = .flagsChanged
    case .mouseMoved, .leftMouseDragged, .rightMouseDragged, .otherMouseDragged:
        kind = .mouseMove
    case .leftMouseDown:
        kind = .mouseButton(button: 0x01, isDown: true)
    case .leftMouseUp:
        kind = .mouseButton(button: 0x01, isDown: false)
    case .rightMouseDown:
        kind = .mouseButton(button: 0x02, isDown: true)
    case .rightMouseUp:
        kind = .mouseButton(button: 0x02, isDown: false)
    case .otherMouseDown:
        kind = .mouseButton(button: 0x03, isDown: true)
    case .otherMouseUp:
        kind = .mouseButton(button: 0x03, isDown: false)
    case .scrollWheel:
        kind = .scroll
    default:
        return Unmanaged.passUnretained(event)
    }

    let sample = CapturedEvent(
        kind: kind,
        virtualKey: event.getIntegerValueField(.keyboardEventKeycode),
        flagsRaw: event.flags.rawValue,
        deltaX: event.getIntegerValueField(.mouseEventDeltaX),
        deltaY: event.getIntegerValueField(.mouseEventDeltaY)
    )
    // scroll 用轴整数（像素/行），mouseMove 用事件增量；两者字段相同位置不同，按 kind 取值已在 handle 内处理
    let scrollSample = CapturedEvent(
        kind: kind,
        virtualKey: sample.virtualKey,
        flagsRaw: sample.flagsRaw,
        deltaX: event.getIntegerValueField(.scrollWheelEventDeltaAxis2),
        deltaY: event.getIntegerValueField(.scrollWheelEventDeltaAxis1)
    )
    let effective = { () -> CapturedEvent in
        if case .scroll = kind { return scrollSample }
        return sample
    }()

    let swallow = MainActor.assumeIsolated {
        tap.handle(effective)
    }
    return swallow ? nil : Unmanaged.passUnretained(event)
}
