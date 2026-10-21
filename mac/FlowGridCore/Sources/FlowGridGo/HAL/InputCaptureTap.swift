import AppKit
import ApplicationServices
import CoreGraphics
import FlowGridCore
import Foundation

/// 注入事件打的标记（CGEvent.eventSourceUserData）。捕获 tap 看到这个值就
/// 直接放行——否则注入到本机的事件会再次被自己的 tap 抓走并转发回对端，
/// 双向/同机联调时会形成无限回环。取 "FLOW" 的 ASCII。
enum InputMarker {
    static let injected: Int64 = 0x464C_4F57
}

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
    private var targetPlatform: (() -> FGPlatform)?

    static var isTrusted: Bool {
        AXIsProcessTrusted()
    }

    /// targetPlatform 在每次按键时读取（握手完成后才会知道对端平台）。
    /// 跨平台时 Ctrl↔Cmd 的默认换算在这里做；用户自定义 keymap 规则由
    /// 会话层另行覆盖。
    func start(targetPlatform: @escaping () -> FGPlatform,
               send: @escaping (UInt8, Data) -> Void) -> Bool {
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
        self.targetPlatform = targetPlatform
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
        targetPlatform = nil
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
        let target = targetPlatform?() ?? .macOS
        switch sample.kind {
        case .keyDown, .keyUp:
            let virtualKey = CGKeyCode(sample.virtualKey)
            guard let rawUsage = KeyCodeTable.hidUsage(forVirtualKey: virtualKey) else {
                return true // 未知键吞掉，避免本机与对端行为分叉
            }
            // 修饰键本身也是 KEY_DOWN/UP 事件：位掩码换了平台，键位也得换
            // （mac 的 Cmd ↔ Windows 的 Ctrl/Win 语义，见 §6.3）。
            let usage = KeyCodeTable.remapModifierKey(rawUsage, from: .macOS, to: target)
            let modifiers = GoProtocol.remapModifiers(
                KeyCodeTable.modifierMask(from: flags), from: .macOS, to: target)
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
            guard let rawUsage = KeyCodeTable.hidUsage(forVirtualKey: virtualKey) else {
                return false
            }
            let usage = KeyCodeTable.remapModifierKey(rawUsage, from: .macOS, to: target)
            let modifiers = GoProtocol.remapModifiers(
                KeyCodeTable.modifierMask(from: flags), from: .macOS, to: target)
            // Caps Lock 是 toggle：每次 flagsChanged 都是一次「按下」，不按
            // 物理键的 down/up 成对发送，目标端会一直停在按下状态。
            if rawUsage == 0x39 {
                if let down = try? GoPayloads.keyDown(keyCode: usage, modifiers: modifiers) {
                    send?(FGFrameType.keyDown, down)
                }
                if let up = try? GoPayloads.keyUp(keyCode: usage) {
                    send?(FGFrameType.keyUp, up)
                }
                return true
            }
            // 按下/松开必须用未换位的 rawUsage 判断：Ctrl 换成 Cmd 后，
            // flags 里出现的是 maskControl 而不是 maskCommand。
            let pressed: Bool
            switch rawUsage {
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
            guard sample.deltaX != 0 || sample.deltaY != 0 else { return true }
            if let payload = try? GoPayloads.scroll(
                deltaY: Int16(clamping: sample.deltaY), deltaX: Int16(clamping: sample.deltaX)
            ) {
                send?(FGFrameType.scroll, payload)
            }
            return true
        }
    }
}

/// CGEvent 的 mouseEventButtonNumber → 协议按钮号（§6.4）：
/// 0=左 1=右 2=中 3=后退 4=前进。其他键位按中键处理，至少不丢事件。
private func mouseButtonID(_ number: Int64) -> UInt8 {
    switch number {
    case 3: return 0x04
    case 4: return 0x05
    default: return 0x03
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

    // 注入事件带本进程标记：直接放行，既不转发也不吞。否则注入到本机
    // 的事件会被同一个 tap 再捕获，双向/同机联调时形成回环。
    if event.getIntegerValueField(.eventSourceUserData) == InputMarker.injected {
        return Unmanaged.passUnretained(event)
    }

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
        kind = .mouseButton(button: mouseButtonID(event.getIntegerValueField(.mouseEventButtonNumber)),
                            isDown: true)
    case .otherMouseUp:
        kind = .mouseButton(button: mouseButtonID(event.getIntegerValueField(.mouseEventButtonNumber)),
                            isDown: false)
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
    // scroll 用轴整数（行/格），mouseMove 用事件增量；两者字段相同位置不同，按 kind 取值已在 handle 内处理
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
