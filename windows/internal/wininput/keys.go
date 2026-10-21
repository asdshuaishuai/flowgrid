// HID Usage ID ↔ Windows VK 映射（§6.1）。纯函数，可单测。
package wininput

import (
	"sort"

	"github.com/flowgrid/protocol"
)

// vk 常量（winuser.h；避免引入 user32 包依赖）。
const (
	vkCancel       = 0x03
	vkBack         = 0x08
	vkTab          = 0x09
	vkReturn       = 0x0D
	vkShift        = 0x10
	vkControl      = 0x11
	vkMenu         = 0x12
	vkPause        = 0x13
	vkCapital      = 0x14
	vkEscape       = 0x15
	vkSpace        = 0x20
	vkPrior        = 0x21
	vkNext         = 0x22
	vkEnd          = 0x23
	vkHome         = 0x24
	vkLeft         = 0x25
	vkUp           = 0x26
	vkRight        = 0x27
	vkDown         = 0x28
	vkSnapshot     = 0x2C // VK_SNAPSHOT（0x2A 是未定义值，早先写错）
	vkInsert       = 0x2D
	vkDelete       = 0x2E
	vkHelp         = 0x2F
	vkLWin         = 0x5B
	vkRWin         = 0x5C
	vkApps         = 0x5D
	vkNumpad0      = 0x60
	vkDecimal      = 0x6E // VK_DECIMAL（小键盘 . / Del）
	vkDivide       = 0x6F // VK_DIVIDE（小键盘 /，扩展键）
	vkF1           = 0x70
	vkNumLock      = 0x90
	vkScroll       = 0x91
	vkLShift       = 0xA0
	vkRShift       = 0xA1
	vkLControl     = 0xA2
	vkRControl     = 0xA3
	vkLMenu        = 0xA4
	vkRMenu        = 0xA5
	vkOemSemicolon = 0xBA
	vkOemPlus      = 0xBB
	vkOemComma     = 0xBC
	vkOemMinus     = 0xBD
	vkOemPeriod    = 0xBE
	vkOemQuestion  = 0xBF
	vkOemTilde     = 0xC0
	vkOemOpenBrks  = 0xDB
	vkOemPipe      = 0xDC
	vkOemCloseBrks = 0xDD
	vkOemQuotes    = 0xDE
	vkOemBackslash = 0xDC // VK_OEM_5（US 布局反斜杠/竖线，不是 0xE2）
	vkOem102       = 0xE2 // ISO 额外键（HID 0x64）
)

var hidToVK = map[uint16]uint16{
	protocol.HIDKeyA: 'A', protocol.HIDKeyB: 'B', protocol.HIDKeyC: 'C',
	protocol.HIDKeyD: 'D', protocol.HIDKeyE: 'E', protocol.HIDKeyF: 'F',
	protocol.HIDKeyG: 'G', protocol.HIDKeyH: 'H', protocol.HIDKeyI: 'I',
	protocol.HIDKeyJ: 'J', protocol.HIDKeyK: 'K', protocol.HIDKeyL: 'L',
	protocol.HIDKeyM: 'M', protocol.HIDKeyN: 'N', protocol.HIDKeyO: 'O',
	protocol.HIDKeyP: 'P', protocol.HIDKeyQ: 'Q', protocol.HIDKeyR: 'R',
	protocol.HIDKeyS: 'S', protocol.HIDKeyT: 'T', protocol.HIDKeyU: 'U',
	protocol.HIDKeyV: 'V', protocol.HIDKeyW: 'W', protocol.HIDKeyX: 'X',
	protocol.HIDKeyY: 'Y', protocol.HIDKeyZ: 'Z',
	protocol.HIDKey1: '1', protocol.HIDKey2: '2', protocol.HIDKey3: '3',
	protocol.HIDKey4: '4', protocol.HIDKey5: '5', protocol.HIDKey6: '6',
	protocol.HIDKey7: '7', protocol.HIDKey8: '8', protocol.HIDKey9: '9',
	protocol.HIDKey0:         '0',
	protocol.HIDKeyEnter:     vkReturn,
	protocol.HIDKeyEscape:    vkEscape,
	protocol.HIDKeyBackspace: vkBack,
	protocol.HIDKeyTab:       vkTab,
	protocol.HIDKeySpace:     vkSpace,
	protocol.HIDKeyCapsLock:  vkCapital,
	protocol.HIDKeyF1:        vkF1, protocol.HIDKeyF2: vkF1 + 1,
	protocol.HIDKeyF3: vkF1 + 2, protocol.HIDKeyF4: vkF1 + 3,
	protocol.HIDKeyF5: vkF1 + 4, protocol.HIDKeyF6: vkF1 + 5,
	protocol.HIDKeyF7: vkF1 + 6, protocol.HIDKeyF8: vkF1 + 7,
	protocol.HIDKeyF9: vkF1 + 8, protocol.HIDKeyF10: vkF1 + 9,
	protocol.HIDKeyF11: vkF1 + 10, protocol.HIDKeyF12: vkF1 + 11,
	protocol.HIDKeyPrintScreen: vkSnapshot,
	protocol.HIDKeyScrollLock:  vkScroll,
	protocol.HIDKeyPause:       vkPause,
	protocol.HIDKeyInsert:      vkInsert,
	protocol.HIDKeyHome:        vkHome,
	protocol.HIDKeyPageUp:      vkPrior,
	protocol.HIDKeyDelete:      vkDelete,
	protocol.HIDKeyEnd:         vkEnd,
	protocol.HIDKeyPageDown:    vkNext,
	protocol.HIDKeyRight:       vkRight,
	protocol.HIDKeyLeft:        vkLeft,
	protocol.HIDKeyDown:        vkDown,
	protocol.HIDKeyUp:          vkUp,
	protocol.HIDKeyNumLock:     vkNumLock,
	protocol.HIDKeyKpDivide:    0x6F,
	protocol.HIDKeyKpMultiply:  0x6A,
	protocol.HIDKeyKpMinus:     0x6D,
	protocol.HIDKeyKpPlus:      0x6B,
	protocol.HIDKeyKpEnter:     vkReturn,
	protocol.HIDKeyKp1:         vkNumpad0 + 1, protocol.HIDKeyKp2: vkNumpad0 + 2,
	protocol.HIDKeyKp3: vkNumpad0 + 3, protocol.HIDKeyKp4: vkNumpad0 + 4,
	protocol.HIDKeyKp5: vkNumpad0 + 5, protocol.HIDKeyKp6: vkNumpad0 + 6,
	protocol.HIDKeyKp7: vkNumpad0 + 7, protocol.HIDKeyKp8: vkNumpad0 + 8,
	protocol.HIDKeyKp9: vkNumpad0 + 9, protocol.HIDKeyKp0: vkNumpad0,
	protocol.HIDKeyKpDot: vkDecimal,
	protocol.HIDKeyLCtrl: vkLControl, protocol.HIDKeyLShift: vkLShift,
	protocol.HIDKeyLAlt: vkLMenu, protocol.HIDKeyLMeta: vkLWin,
	protocol.HIDKeyRCtrl: vkRControl, protocol.HIDKeyRShift: vkRShift,
	protocol.HIDKeyRAlt: vkRMenu, protocol.HIDKeyRMeta: vkRWin,
	// US 布局标点（§6.1 HID Keyboard Page 对应位）
	0x2D: vkOemMinus, 0x2E: vkOemPlus, 0x2F: vkOemOpenBrks, 0x30: vkOemCloseBrks,
	// 0x32（Non-US # and ~）在 Windows 上没有独立的 US VK，硬塞给
	// VK_OEM_102 会让 0x64（真正的 102 键）在反查时被 0x32 抢走。
	0x31: vkOemBackslash, 0x33: vkOemSemicolon, 0x34: vkOemQuotes,
	0x35: vkOemTilde, 0x36: vkOemComma, 0x37: vkOemPeriod, 0x38: vkOemQuestion,
	0x64: vkOem102,
}

// extendedVK 需要带 KEYEVENTF_EXTENDEDKEY 的 VK（右侧修饰键、导航键、Ins/Del）。
// 漏标的后果不是「按键不对」而是「变成另一个键」：PrintScreen 的扫描码 0x37
// 不带 E0 就是小键盘 *，NumLock 的 0x45 不带 E0 是 Pause。
var extendedVK = map[uint16]bool{
	vkRControl: true, vkRMenu: true, vkRWin: true, vkLWin: true,
	vkInsert: true, vkHome: true, vkPrior: true, vkDelete: true,
	vkEnd: true, vkNext: true, vkRight: true, vkLeft: true, vkUp: true, vkDown: true,
	vkSnapshot: true, vkNumLock: true, vkDivide: true,
}

// HIDToVK 把 HID Usage ID 转成 VK；未知键返回 false。
func HIDToVK(hid uint16) (uint16, bool) {
	vk, ok := hidToVK[hid]
	return vk, ok
}

// vkToHID 是捕获侧的确定性反查表。不能直接遍历 hidToVK：Enter/KpEnter、
// Delete/KpDot 共用同一个 VK，map 迭代顺序随机会让「按主 Enter」偶尔变成
// 小键盘 Enter。这里按 HID 升序先占，主键区（数值更小）永远赢。
var vkToHID = buildVKToHID()

func buildVKToHID() map[uint16]uint16 {
	hids := make([]int, 0, len(hidToVK))
	for hid := range hidToVK {
		hids = append(hids, int(hid))
	}
	sort.Ints(hids)
	out := make(map[uint16]uint16, len(hids))
	for _, h := range hids {
		hid := uint16(h)
		vk := hidToVK[hid]
		if _, taken := out[vk]; taken {
			continue
		}
		out[vk] = hid
	}
	return out
}

// VKToHID 反向映射（捕获侧）。
func VKToHID(vk uint16) (uint16, bool) {
	hid, ok := vkToHID[vk]
	return hid, ok
}

// IsExtendedVK 报告该 VK 注入时是否需要扩展键标志。
func IsExtendedVK(vk uint16) bool { return extendedVK[vk] }

// IsKeypadEnter 报告该 HID 是否是小键盘 Enter（VK_RETURN 但需扩展键标志）。
func IsKeypadEnter(hid uint16) bool { return hid == protocol.HIDKeyKpEnter }

// wheelLines 把 Windows 滚轮 delta（120 = 一格）折算成协议滚轮单位（1 = 一格），
// 不足一格的余数留在 rem 里等下一次累加。高精度滚轮/触控板不会被截断成 0。
func wheelLines(delta int32, rem *int32) int16 {
	total := *rem + delta
	lines := total / wheelDelta
	*rem = total - lines*wheelDelta
	if lines > 32767 {
		return 32767
	}
	if lines < -32768 {
		return -32768
	}
	return int16(lines)
}

var modifierHIDs = [...]struct {
	bit uint8
	hid uint16
}{
	{protocol.ModLeftCtrl, protocol.HIDKeyLCtrl},
	{protocol.ModLeftShift, protocol.HIDKeyLShift},
	{protocol.ModLeftAlt, protocol.HIDKeyLAlt},
	{protocol.ModLeftMeta, protocol.HIDKeyLMeta},
	{protocol.ModRightCtrl, protocol.HIDKeyRCtrl},
	{protocol.ModRightShift, protocol.HIDKeyRShift},
	{protocol.ModRightAlt, protocol.HIDKeyRAlt},
	{protocol.ModRightMeta, protocol.HIDKeyRMeta},
}

const modifierMaskAll = protocol.ModCtrl | protocol.ModShift | protocol.ModAlt | protocol.ModMeta

// modifierChanges 返回「把注入侧修饰键状态从 current 调成 want」需要补按和
// 松开的 HID 键。KEY_DOWN 只带修饰位而不带真实的修饰键事件（keymap 合成
// Ctrl+C 就是这种情形），接收端必须自己补齐，否则组合键在目标机上不成立。
func modifierChanges(current, want uint8) (press, release []uint16) {
	current &= modifierMaskAll
	want &= modifierMaskAll
	for _, m := range modifierHIDs {
		switch {
		case want&m.bit != 0 && current&m.bit == 0:
			press = append(press, m.hid)
		case want&m.bit == 0 && current&m.bit != 0:
			release = append(release, m.hid)
		}
	}
	return press, release
}

// ModifiersFromVK 把一个修饰键 VK 换成 HID 修饰位掩码（§6.2）。
func ModifiersFromVK(vk uint16) (uint8, bool) {
	switch vk {
	case vkLControl, vkControl:
		return uint8(protocol.ModLeftCtrl), true
	case vkRControl:
		return uint8(protocol.ModRightCtrl), true
	case vkLShift, vkShift:
		return uint8(protocol.ModLeftShift), true
	case vkRShift:
		return uint8(protocol.ModRightShift), true
	case vkLMenu, vkMenu:
		return uint8(protocol.ModLeftAlt), true
	case vkRMenu:
		return uint8(protocol.ModRightAlt), true
	case vkLWin:
		return uint8(protocol.ModLeftMeta), true
	case vkRWin:
		return uint8(protocol.ModRightMeta), true
	}
	return 0, false
}
