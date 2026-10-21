import CoreGraphics
import Testing

import FlowGridCore
import FlowGridGo

/// 键鼠注入/捕获的纯映射回归：跨平台修饰键换算、修饰位分组、小键盘。
/// 这些错了不会崩，只会表现为「快捷键在另一端不生效」，所以必须有单测钉住。
@Suite
struct InputMappingTests {

    @Test func remapModifierKeyAcrossPlatforms() {
        // mac -> Windows/Linux：Cmd 必须变 Ctrl，否则复制粘贴全废。
        #expect(KeyCodeTable.remapModifierKey(0xE3, from: .macOS, to: .windows) == 0xE0)
        #expect(KeyCodeTable.remapModifierKey(0xE7, from: .macOS, to: .linux) == 0xE4)
        // Windows/Linux -> mac：Ctrl 必须变 Cmd。
        #expect(KeyCodeTable.remapModifierKey(0xE0, from: .windows, to: .macOS) == 0xE3)
        #expect(KeyCodeTable.remapModifierKey(0xE4, from: .linux, to: .macOS) == 0xE7)
        // 非 Ctrl/Meta 的修饰键与普通键不动。
        #expect(KeyCodeTable.remapModifierKey(0xE1, from: .windows, to: .macOS) == 0xE1)
        #expect(KeyCodeTable.remapModifierKey(0x04, from: .windows, to: .macOS) == 0x04)
        // 同为 Ctrl 系或同为 Meta 系：不换。
        #expect(KeyCodeTable.remapModifierKey(0xE0, from: .windows, to: .linux) == 0xE0)
        #expect(KeyCodeTable.remapModifierKey(0xE0, from: .macOS, to: .iOS) == 0xE0)
    }

    @Test func modifierGroups() {
        #expect(KeyCodeTable.modifierGroup(forHID: 0xE0) == FGModifier.ctrl)
        #expect(KeyCodeTable.modifierGroup(forHID: 0xE4) == FGModifier.ctrl)
        #expect(KeyCodeTable.modifierGroup(forHID: 0xE1) == FGModifier.shift)
        #expect(KeyCodeTable.modifierGroup(forHID: 0xE6) == FGModifier.alt)
        #expect(KeyCodeTable.modifierGroup(forHID: 0xE7) == FGModifier.meta)
        #expect(KeyCodeTable.modifierGroup(forHID: 0x04) == nil)
        // 单键松开只清自己那一侧。
        #expect(KeyCodeTable.modifierBit(forHID: 0xE0) == FGModifier.leftCtrl)
        #expect(KeyCodeTable.modifierBit(forHID: 0xE4) == FGModifier.rightCtrl)
        #expect(KeyCodeTable.modifierBit(forHID: 0xE3) == FGModifier.leftMeta)
        #expect(KeyCodeTable.modifierBit(forHID: 0x04) == nil)
    }

    @Test func keypadAndEditingKeys() {
        // 小键盘回车与主回车必须是两个 HID，否则目标端会当主回车。
        #expect(KeyCodeTable.virtualKey(forHID: 0x58) == 0x4C)
        #expect(KeyCodeTable.virtualKey(forHID: 0x28) == 0x24)
        // Forward Delete 不能映射成 Backspace。
        #expect(KeyCodeTable.virtualKey(forHID: 0x4C) == 0x75)
        #expect(KeyCodeTable.virtualKey(forHID: 0x2A) == 0x33)
        // 反向：右侧修饰键不能被合并成左侧。
        #expect(KeyCodeTable.hidUsage(forVirtualKey: 0x3E) == 0xE4)
        #expect(KeyCodeTable.hidUsage(forVirtualKey: 0x36) == 0xE7)
    }
}
