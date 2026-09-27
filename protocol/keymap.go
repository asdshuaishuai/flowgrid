package protocol

// HID Usage ID constants for Keyboard/Keypad Page 0x07 (§6.1).
const (
	// Letters
	HIDKeyA uint16 = 0x04
	HIDKeyB uint16 = 0x05
	HIDKeyC uint16 = 0x06
	HIDKeyD uint16 = 0x07
	HIDKeyE uint16 = 0x08
	HIDKeyF uint16 = 0x09
	HIDKeyG uint16 = 0x0A
	HIDKeyH uint16 = 0x0B
	HIDKeyI uint16 = 0x0C
	HIDKeyJ uint16 = 0x0D
	HIDKeyK uint16 = 0x0E
	HIDKeyL uint16 = 0x0F
	HIDKeyM uint16 = 0x10
	HIDKeyN uint16 = 0x11
	HIDKeyO uint16 = 0x12
	HIDKeyP uint16 = 0x13
	HIDKeyQ uint16 = 0x14
	HIDKeyR uint16 = 0x15
	HIDKeyS uint16 = 0x16
	HIDKeyT uint16 = 0x17
	HIDKeyU uint16 = 0x18
	HIDKeyV uint16 = 0x19
	HIDKeyW uint16 = 0x1A
	HIDKeyX uint16 = 0x1B
	HIDKeyY uint16 = 0x1C
	HIDKeyZ uint16 = 0x1D

	// Numbers
	HIDKey1 uint16 = 0x1E
	HIDKey2 uint16 = 0x1F
	HIDKey3 uint16 = 0x20
	HIDKey4 uint16 = 0x21
	HIDKey5 uint16 = 0x22
	HIDKey6 uint16 = 0x23
	HIDKey7 uint16 = 0x24
	HIDKey8 uint16 = 0x25
	HIDKey9 uint16 = 0x26
	HIDKey0 uint16 = 0x27

	// Special keys
	HIDKeyEnter      uint16 = 0x28
	HIDKeyEscape     uint16 = 0x29
	HIDKeyBackspace  uint16 = 0x2A
	HIDKeyTab        uint16 = 0x2B
	HIDKeySpace      uint16 = 0x2C
	HIDKeyCapsLock   uint16 = 0x39

	// Function keys
	HIDKeyF1  uint16 = 0x3A
	HIDKeyF2  uint16 = 0x3B
	HIDKeyF3  uint16 = 0x3C
	HIDKeyF4  uint16 = 0x3D
	HIDKeyF5  uint16 = 0x3E
	HIDKeyF6  uint16 = 0x3F
	HIDKeyF7  uint16 = 0x40
	HIDKeyF8  uint16 = 0x41
	HIDKeyF9  uint16 = 0x42
	HIDKeyF10 uint16 = 0x43
	HIDKeyF11 uint16 = 0x44
	HIDKeyF12 uint16 = 0x45

	// Navigation
	HIDKeyPrintScreen uint16 = 0x46
	HIDKeyScrollLock  uint16 = 0x47
	HIDKeyPause       uint16 = 0x48
	HIDKeyInsert      uint16 = 0x49
	HIDKeyHome        uint16 = 0x4A
	HIDKeyPageUp      uint16 = 0x4B
	HIDKeyDelete      uint16 = 0x4C
	HIDKeyEnd         uint16 = 0x4D
	HIDKeyPageDown    uint16 = 0x4E

	// Arrow keys
	HIDKeyRight uint16 = 0x4F
	HIDKeyLeft  uint16 = 0x50
	HIDKeyDown  uint16 = 0x51
	HIDKeyUp    uint16 = 0x52

	// Keypad
	HIDKeyNumLock    uint16 = 0x53
	HIDKeyKpDivide   uint16 = 0x54
	HIDKeyKpMultiply uint16 = 0x55
	HIDKeyKpMinus    uint16 = 0x56
	HIDKeyKpPlus     uint16 = 0x57
	HIDKeyKpEnter    uint16 = 0x58
	HIDKeyKp1        uint16 = 0x59
	HIDKeyKp2        uint16 = 0x5A
	HIDKeyKp3        uint16 = 0x5B
	HIDKeyKp4        uint16 = 0x5C
	HIDKeyKp5        uint16 = 0x5D
	HIDKeyKp6        uint16 = 0x5E
	HIDKeyKp7        uint16 = 0x5F
	HIDKeyKp8        uint16 = 0x60
	HIDKeyKp9        uint16 = 0x61
	HIDKeyKp0        uint16 = 0x62
	HIDKeyKpDot      uint16 = 0x63
)

// Modifier bit masks (§6.2).
const (
	ModLeftCtrl  uint8 = 0x01
	ModLeftShift uint8 = 0x02
	ModLeftAlt   uint8 = 0x04
	ModLeftMeta  uint8 = 0x08
	ModRightCtrl uint8 = 0x10
	ModRightShift uint8 = 0x20
	ModRightAlt  uint8 = 0x40
	ModRightMeta uint8 = 0x80
)

// Platform-specific modifier key Usage IDs (§6.4). HID frames always carry the
// standard masks above; these codes matter at the KeyMapper layer.
const (
	HIDKeyLCtrl  uint16 = 0xE0 // Windows/Linux LCtrl
	HIDKeyLShift uint16 = 0xE1
	HIDKeyLAlt   uint16 = 0xE2 // Windows LMenu / macOS LOption
	HIDKeyLMeta  uint16 = 0xE3 // Windows LWin / macOS Cmd / Linux Super
	HIDKeyRCtrl  uint16 = 0xE4
	HIDKeyRShift uint16 = 0xE5
	HIDKeyRAlt   uint16 = 0xE6 // AltGr
	HIDKeyRMeta  uint16 = 0xE7
	HIDKeyFn     uint16 = 0x3F // macOS only
)

// Convenience combined masks.
const (
	ModCtrl  = ModLeftCtrl | ModRightCtrl
	ModShift = ModLeftShift | ModRightShift
	ModAlt   = ModLeftAlt | ModRightAlt
	ModMeta  = ModLeftMeta | ModRightMeta
)

// usesMetaModifier returns true if the platform uses Meta (Cmd/Super) as the primary
// command modifier (macOS, iOS), as opposed to Ctrl (Windows, Linux, Android).
func usesMetaModifier(p Platform) bool {
	return p == PlatformMacOS || p == PlatformIOS
}

// RemapModifiers remaps modifier keys between platforms (§6.3).
// macOS/iOS Cmd <-> Windows/Linux/Android Ctrl. Shift and Alt pass through unchanged.
func RemapModifiers(mods uint8, fromPlatform, toPlatform Platform) uint8 {
	if fromPlatform == toPlatform {
		return mods
	}

	fromMeta := usesMetaModifier(fromPlatform)
	toMeta := usesMetaModifier(toPlatform)

	if fromMeta == toMeta {
		return mods
	}

	result := mods & ^(ModLeftCtrl | ModRightCtrl | ModLeftMeta | ModRightMeta)

	if fromMeta {
		// Meta platform -> Ctrl platform: Cmd becomes Ctrl
		if mods&ModLeftMeta != 0 {
			result |= ModLeftCtrl
		}
		if mods&ModRightMeta != 0 {
			result |= ModRightCtrl
		}
	} else {
		// Ctrl platform -> Meta platform: Ctrl becomes Cmd
		if mods&ModLeftCtrl != 0 {
			result |= ModLeftMeta
		}
		if mods&ModRightCtrl != 0 {
			result |= ModRightMeta
		}
	}

	return result
}

// ModifierFromSingleKey returns the modifier mask for a single modifier HID key,
// or 0 if it's not a modifier key.
func ModifierFromSingleKey(hidKey uint16) uint8 {
	switch hidKey {
	case HIDKeyLCtrl:
		return ModLeftCtrl
	case HIDKeyLShift:
		return ModLeftShift
	case HIDKeyLAlt:
		return ModLeftAlt
	case HIDKeyLMeta:
		return ModLeftMeta
	case HIDKeyRCtrl:
		return ModRightCtrl
	case HIDKeyRShift:
		return ModRightShift
	case HIDKeyRAlt:
		return ModRightAlt
	case HIDKeyRMeta:
		return ModRightMeta
	default:
		return 0
	}
}
