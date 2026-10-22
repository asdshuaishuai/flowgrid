import CoreGraphics
import Testing

import FlowGridCore
import FlowGridGo

/// 键鼠注入/捕获的纯映射回归：跨平台修饰键换算、修饰位分组、小键盘。
/// 这些错了不会崩，只会表现为「快捷键在另一端不生效」，所以必须有单测钉住。
@Suite
struct InputMappingTests {

    @Test func remapCapturedKeyAcrossPlatforms() {
        // 键码 + 掩码一起按目标平台重映射，能力在协议层（fg_input_remap_key → RemapCapturedKey）。
        // mac -> Linux：Cmd+C 必须变 Ctrl+C，否则复制粘贴全废。
        let cc = GoProtocol.remapCapturedKey(0x06, modifiers: FGModifier.leftMeta, from: .macOS, to: .linux)
        #expect(cc.key == 0x06)
        #expect(cc.mods == FGModifier.leftCtrl)
        // 修饰键本身也是 KEY_DOWN：LMeta -> LCtrl，掩码 Meta -> Ctrl。
        let meta = GoProtocol.remapCapturedKey(0xE3, modifiers: FGModifier.leftMeta, from: .macOS, to: .linux)
        #expect(meta.key == 0xE0)
        #expect(meta.mods == FGModifier.leftCtrl)
        // Linux -> mac：Ctrl+C 必须变 Cmd+C。
        let back = GoProtocol.remapCapturedKey(0x06, modifiers: FGModifier.leftCtrl, from: .linux, to: .macOS)
        #expect(back.key == 0x06)
        #expect(back.mods == FGModifier.leftMeta)
        // 非 Ctrl/Meta 的修饰键与普通键不动；Shift/Alt 掩码直通。
        let shift = GoProtocol.remapCapturedKey(0x04, modifiers: FGModifier.leftShift, from: .windows, to: .macOS)
        #expect(shift.key == 0x04)
        #expect(shift.mods == FGModifier.leftShift)
        // 同为 Ctrl 系（Windows↔Linux）：不换。
        let same = GoProtocol.remapCapturedKey(0xE0, modifiers: FGModifier.leftCtrl, from: .windows, to: .linux)
        #expect(same.key == 0xE0)
        #expect(same.mods == FGModifier.leftCtrl)
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
