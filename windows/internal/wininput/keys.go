// HID Usage ID ↔ Windows VK 映射（§6.1）。纯函数，可单测。
package wininput

import "github.com/flowgrid/protocol"

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
	vkInsert       = 0x2D
	vkDelete       = 0x2E
	vkHelp         = 0x2F
	vkLWin         = 0x5B
	vkRWin         = 0x5C
	vkApps         = 0x5D
	vkNumpad0      = 0x60
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
	vkOemBackslash = 0xE2
	vkOem102       = 0xE2
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
	protocol.HIDKeyPrintScreen: 0x2A, // SnapShot
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
	protocol.HIDKeyKpDot: 0x2E, // vkDelete 即小键盘点
	protocol.HIDKeyLCtrl: vkLControl, protocol.HIDKeyLShift: vkLShift,
	protocol.HIDKeyLAlt: vkLMenu, protocol.HIDKeyLMeta: vkLWin,
	protocol.HIDKeyRCtrl: vkRControl, protocol.HIDKeyRShift: vkRShift,
	protocol.HIDKeyRAlt: vkRMenu, protocol.HIDKeyRMeta: vkRWin,
	// US 布局标点（§6.1 HID Keyboard Page 对应位）
	0x2D: vkOemMinus, 0x2E: vkOemPlus, 0x2F: vkOemOpenBrks, 0x30: vkOemCloseBrks,
	0x31: vkOemBackslash, 0x32: vkOem102, 0x33: vkOemSemicolon, 0x34: vkOemQuotes,
	0x35: vkOemTilde, 0x36: vkOemComma, 0x37: vkOemPeriod, 0x38: vkOemQuestion,
}

// extendedVK 需要带 KEYEVENTF_EXTENDEDKEY 的 VK（右侧修饰键、导航键、Ins/Del）。
var extendedVK = map[uint16]bool{
	vkRControl: true, vkRShift: false, vkRMenu: true, vkRWin: true,
	vkInsert: true, vkHome: true, vkPrior: true, vkDelete: true,
	vkEnd: true, vkNext: true, vkRight: true, vkLeft: true, vkUp: true, vkDown: true,
}

// HIDToVK 把 HID Usage ID 转成 VK；未知键返回 false。
func HIDToVK(hid uint16) (uint16, bool) {
	vk, ok := hidToVK[hid]
	return vk, ok
}

// VKToHID 反向映射（捕获侧）。
func VKToHID(vk uint16) (uint16, bool) {
	for hid, v := range hidToVK {
		if v == vk {
			return hid, true
		}
	}
	return 0, false
}

// IsExtendedVK 报告该 VK 注入时是否需要扩展键标志。
func IsExtendedVK(vk uint16) bool { return extendedVK[vk] }

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
