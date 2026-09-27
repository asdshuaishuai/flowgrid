//! Complete bidirectional mapping between USB HID keyboard Usage IDs (§6.1)
//! and Linux evdev keycodes. Used by the injection HAL (HID -> Linux) and the
//! Host-side capture module (Linux -> HID).

/// (HID usage, Linux keycode) pairs. Keep sorted by HID usage.
const HID_LINUX_TABLE: &[(u16, u16)] = &[
    (0x04, 30),  // A
    (0x05, 48),  // B
    (0x06, 46),  // C
    (0x07, 32),  // D
    (0x08, 18),  // E
    (0x09, 33),  // F
    (0x0A, 34),  // G
    (0x0B, 35),  // H
    (0x0C, 23),  // I
    (0x0D, 36),  // J
    (0x0E, 37),  // K
    (0x0F, 38),  // L
    (0x10, 50),  // M
    (0x11, 49),  // N
    (0x12, 24),  // O
    (0x13, 25),  // P
    (0x14, 16),  // Q
    (0x15, 19),  // R
    (0x16, 31),  // S
    (0x17, 20),  // T
    (0x18, 22),  // U
    (0x19, 47),  // V
    (0x1A, 17),  // W
    (0x1B, 45),  // X
    (0x1C, 21),  // Y
    (0x1D, 44),  // Z
    (0x1E, 2),   // 1
    (0x1F, 3),   // 2
    (0x20, 4),   // 3
    (0x21, 5),   // 4
    (0x22, 6),   // 5
    (0x23, 7),   // 6
    (0x24, 8),   // 7
    (0x25, 9),   // 8
    (0x26, 10),  // 9
    (0x27, 11),  // 0
    (0x28, 28),  // Enter
    (0x29, 1),   // Escape
    (0x2A, 14),  // Backspace
    (0x2B, 15),  // Tab
    (0x2C, 57),  // Space
    (0x2D, 12),  // Minus
    (0x2E, 13),  // Equal
    (0x2F, 26),  // Left Brace
    (0x30, 27),  // Right Brace
    (0x31, 43),  // Backslash
    (0x33, 39),  // Semicolon
    (0x34, 40),  // Apostrophe
    (0x35, 41),  // Grave
    (0x36, 51),  // Comma
    (0x37, 52),  // Dot
    (0x38, 53),  // Slash
    (0x39, 58),  // Caps Lock
    (0x3A, 59),  // F1
    (0x3B, 60),  // F2
    (0x3C, 61),  // F3
    (0x3D, 62),  // F4
    (0x3E, 63),  // F5
    (0x3F, 64),  // F6
    (0x40, 65),  // F7
    (0x41, 66),  // F8
    (0x42, 67),  // F9
    (0x43, 68),  // F10
    (0x44, 87),  // F11
    (0x45, 88),  // F12
    (0x46, 99),  // Print Screen
    (0x47, 70),  // Scroll Lock
    (0x48, 119), // Pause
    (0x49, 110), // Insert
    (0x4A, 102), // Home
    (0x4B, 104), // Page Up
    (0x4C, 111), // Delete
    (0x4D, 107), // End
    (0x4E, 109), // Page Down
    (0x4F, 106), // Right Arrow
    (0x50, 105), // Left Arrow
    (0x51, 108), // Down Arrow
    (0x52, 103), // Up Arrow
    (0x53, 69),  // Num Lock
    (0x54, 98),  // Keypad /
    (0x55, 55),  // Keypad *
    (0x56, 74),  // Keypad -
    (0x57, 78),  // Keypad +
    (0x58, 96),  // Keypad Enter
    (0x59, 79),  // Keypad 1
    (0x5A, 80),  // Keypad 2
    (0x5B, 81),  // Keypad 3
    (0x5C, 75),  // Keypad 4
    (0x5D, 76),  // Keypad 5
    (0x5E, 77),  // Keypad 6
    (0x5F, 71),  // Keypad 7
    (0x60, 72),  // Keypad 8
    (0x61, 73),  // Keypad 9
    (0x62, 82),  // Keypad 0
    (0x63, 83),  // Keypad .
    (0x64, 86),  // Non-US Backslash
    (0x65, 139), // Application (Menu)
    (0x66, 116), // Power
    (0x68, 183), // F13
    (0x69, 184), // F14
    (0x6A, 185), // F15
    (0x6B, 186), // F16
    (0x6C, 187), // F17
    (0x6D, 188), // F18
    (0x6E, 189), // F19
    (0x6F, 190), // F20
    (0x70, 191), // F21
    (0x71, 192), // F22
    (0x72, 193), // F23
    (0x73, 194), // F24
    (0xE0, 29),  // Left Control
    (0xE1, 42),  // Left Shift
    (0xE2, 56),  // Left Alt
    (0xE3, 125), // Left Meta
    (0xE4, 97),  // Right Control
    (0xE5, 54),  // Right Shift
    (0xE6, 100), // Right Alt (AltGr)
    (0xE7, 126), // Right Meta
];

pub(crate) fn hid_to_linux_keycode(hid: u16) -> Option<u16> {
    HID_LINUX_TABLE
        .iter()
        .find(|(h, _)| *h == hid)
        .map(|(_, l)| *l)
}

pub(crate) fn linux_to_hid_keycode(code: u16) -> Option<u16> {
    HID_LINUX_TABLE
        .iter()
        .find(|(_, l)| *l == code)
        .map(|(h, _)| *h)
}
