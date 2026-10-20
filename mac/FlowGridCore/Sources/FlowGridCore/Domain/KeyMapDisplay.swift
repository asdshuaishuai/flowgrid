import Foundation

/// 键位映射的一行（全局网格或应用覆盖的展示单元）。
/// 内容由真实 KeyMapTable 的 Lookup 结果驱动（见 FlowGridGo.GoKeyMapRows）。
public struct KeyMappingRow: Identifiable, Equatable, Sendable {
    public var id: String
    /// 源键位展示（"Ctrl" / "Win" / "⌘"）。
    public var fromKey: String
    /// 目标键位展示；nil 表示未设置（ghost 占位）。
    public var toKey: String?
    /// 用途说明（"复制/粘贴"）。
    public var caption: String

    public init(id: String, fromKey: String, toKey: String?, caption: String) {
        self.id = id
        self.fromKey = fromKey
        self.toKey = toKey
        self.caption = caption
    }

    public var isGhost: Bool { toKey == nil }
}

/// 应用覆盖行（按应用自动切换规则）。
public struct AppOverrideRow: Identifiable, Equatable, Sendable {
    public var id: String
    public var appName: String
    public var note: String
    public var enabled: Bool

    public init(id: String, appName: String, note: String, enabled: Bool) {
        self.id = id
        self.appName = appName
        self.note = note
        self.enabled = enabled
    }
}

/// HID Usage ID → 修饰键展示字形（按目标平台取字）。
public enum ModifierGlyph {
    /// 目标 macOS：Ctrl ⌥ ⇧ ⌘。
    public static func label(forTarget target: FGPlatform, keyCode: UInt16) -> String? {
        switch target {
        case .macOS, .iOS:
            switch keyCode {
            case 0xE0, 0xE4: "Ctrl"
            case 0xE2, 0xE6: "⌥"
            case 0xE1, 0xE5: "⇧"
            case 0xE3, 0xE7: "⌘"
            default: nil
            }
        case .windows:
            switch keyCode {
            case 0xE0, 0xE4: "Ctrl"
            case 0xE2, 0xE6: "Alt"
            case 0xE1, 0xE5: "Shift"
            case 0xE3, 0xE7: "Win"
            default: nil
            }
        case .linux, .android:
            switch keyCode {
            case 0xE0, 0xE4: "Ctrl"
            case 0xE2, 0xE6: "Alt"
            case 0xE1, 0xE5: "Shift"
            case 0xE3, 0xE7: "Super"
            default: nil
            }
        }
    }

    /// 源平台展示（快捷键符号跟随"被控制的键帽"）。
    public static func sourceLabel(forSource source: FGPlatform, keyCode: UInt16) -> String {
        switch source {
        case .macOS, .iOS:
            switch keyCode {
            case 0xE3, 0xE7: return "⌘"
            case 0xE2, 0xE6: return "⌥"
            case 0xE0, 0xE4: return "Ctrl"
            case 0xE1, 0xE5: return "⇧"
            default: break
            }
        case .windows:
            switch keyCode {
            case 0xE3, 0xE7: return "Win"
            case 0xE2, 0xE6: return "Alt"
            case 0xE0, 0xE4: return "Ctrl"
            case 0xE1, 0xE5: return "Shift"
            default: break
            }
        case .linux, .android:
            switch keyCode {
            case 0xE3, 0xE7: return "Super"
            case 0xE2, 0xE6: return "Alt"
            case 0xE0, 0xE4: return "Ctrl"
            case 0xE1, 0xE5: return "Shift"
            default: break
            }
        }
        return String(format: "0x%02X", keyCode)
    }
}
