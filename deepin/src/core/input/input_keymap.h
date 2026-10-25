// Linux 键鼠软模拟的纯映射表：USB HID Usage ID（协议 §6.1）↔ evdev keycode。
//
// 只做查表与位运算，不碰文件描述符、不依赖 Qt，方便在任意 Linux 环境里
// 单独编译和跑单测（deepin/tests/input_keymap_test.cpp）。capture 与
// injection 两条链路共用这一张表，避免「抓到的键和注入的键不是同一个」。
#ifndef FLOWGRID_INPUT_KEYMAP_H
#define FLOWGRID_INPUT_KEYMAP_H

#include <linux/input-event-codes.h>

#include <cstddef>
#include <cstdint>

namespace fg::input {

// 协议 §6.2 修饰位。左右位分开，和 keymap.go 的常量同值。
inline constexpr std::uint8_t kModLeftCtrl = 0x01;
inline constexpr std::uint8_t kModLeftShift = 0x02;
inline constexpr std::uint8_t kModLeftAlt = 0x04;
inline constexpr std::uint8_t kModLeftMeta = 0x08;
inline constexpr std::uint8_t kModRightCtrl = 0x10;
inline constexpr std::uint8_t kModRightShift = 0x20;
inline constexpr std::uint8_t kModRightAlt = 0x40;
inline constexpr std::uint8_t kModRightMeta = 0x80;
inline constexpr std::uint8_t kModCtrl = kModLeftCtrl | kModRightCtrl;
inline constexpr std::uint8_t kModShift = kModLeftShift | kModRightShift;
inline constexpr std::uint8_t kModAlt = kModLeftAlt | kModRightAlt;
inline constexpr std::uint8_t kModMeta = kModLeftMeta | kModRightMeta;

struct HidEvdevPair {
    std::uint16_t hid;
    std::uint16_t evdev;
};

// 按 HID 升序排列；HID 0x00/0x01 是错误/滚动锁定占位，不映射。
inline constexpr HidEvdevPair kHidToEvdevTable[] = {
    {0x04, KEY_A}, {0x05, KEY_B}, {0x06, KEY_C}, {0x07, KEY_D}, {0x08, KEY_E},
    {0x09, KEY_F}, {0x0A, KEY_G}, {0x0B, KEY_H}, {0x0C, KEY_I}, {0x0D, KEY_J},
    {0x0E, KEY_K}, {0x0F, KEY_L}, {0x10, KEY_M}, {0x11, KEY_N}, {0x12, KEY_O},
    {0x13, KEY_P}, {0x14, KEY_Q}, {0x15, KEY_R}, {0x16, KEY_S}, {0x17, KEY_T},
    {0x18, KEY_U}, {0x19, KEY_V}, {0x1A, KEY_W}, {0x1B, KEY_X}, {0x1C, KEY_Y},
    {0x1D, KEY_Z},
    {0x1E, KEY_1}, {0x1F, KEY_2}, {0x20, KEY_3}, {0x21, KEY_4}, {0x22, KEY_5},
    {0x23, KEY_6}, {0x24, KEY_7}, {0x25, KEY_8}, {0x26, KEY_9}, {0x27, KEY_0},
    {0x28, KEY_ENTER}, {0x29, KEY_ESC}, {0x2A, KEY_BACKSPACE}, {0x2B, KEY_TAB},
    {0x2C, KEY_SPACE}, {0x2D, KEY_MINUS}, {0x2E, KEY_EQUAL},
    {0x2F, KEY_LEFTBRACE}, {0x30, KEY_RIGHTBRACE}, {0x31, KEY_BACKSLASH},
    {0x33, KEY_SEMICOLON}, {0x34, KEY_APOSTROPHE}, {0x35, KEY_GRAVE},
    {0x36, KEY_COMMA}, {0x37, KEY_DOT}, {0x38, KEY_SLASH},
    {0x39, KEY_CAPSLOCK},
    {0x3A, KEY_F1}, {0x3B, KEY_F2}, {0x3C, KEY_F3}, {0x3D, KEY_F4},
    {0x3E, KEY_F5}, {0x3F, KEY_F6}, {0x40, KEY_F7}, {0x41, KEY_F8},
    {0x42, KEY_F9}, {0x43, KEY_F10}, {0x44, KEY_F11}, {0x45, KEY_F12},
    {0x46, KEY_SYSRQ}, {0x47, KEY_SCROLLLOCK}, {0x48, KEY_PAUSE},
    {0x49, KEY_INSERT}, {0x4A, KEY_HOME}, {0x4B, KEY_PAGEUP},
    {0x4C, KEY_DELETE}, {0x4D, KEY_END}, {0x4E, KEY_PAGEDOWN},
    {0x4F, KEY_RIGHT}, {0x50, KEY_LEFT}, {0x51, KEY_DOWN}, {0x52, KEY_UP},
    {0x53, KEY_NUMLOCK}, {0x54, KEY_KPSLASH}, {0x55, KEY_KPASTERISK},
    {0x56, KEY_KPMINUS}, {0x57, KEY_KPPLUS}, {0x58, KEY_KPENTER},
    {0x59, KEY_KP1}, {0x5A, KEY_KP2}, {0x5B, KEY_KP3}, {0x5C, KEY_KP4},
    {0x5D, KEY_KP5}, {0x5E, KEY_KP6}, {0x5F, KEY_KP7}, {0x60, KEY_KP8},
    {0x61, KEY_KP9}, {0x62, KEY_KP0}, {0x63, KEY_KPDOT},
    {0x64, KEY_102ND}, {0x65, KEY_COMPOSE}, {0x66, KEY_POWER},
    {0x68, KEY_F13}, {0x69, KEY_F14}, {0x6A, KEY_F15}, {0x6B, KEY_F16},
    {0x6C, KEY_F17}, {0x6D, KEY_F18}, {0x6E, KEY_F19}, {0x6F, KEY_F20},
    {0x70, KEY_F21}, {0x71, KEY_F22}, {0x72, KEY_F23}, {0x73, KEY_F24},
    {0xE0, KEY_LEFTCTRL}, {0xE1, KEY_LEFTSHIFT}, {0xE2, KEY_LEFTALT},
    {0xE3, KEY_LEFTMETA}, {0xE4, KEY_RIGHTCTRL}, {0xE5, KEY_RIGHTSHIFT},
    {0xE6, KEY_RIGHTALT}, {0xE7, KEY_RIGHTMETA},
};

inline constexpr std::uint16_t hidToEvdev(std::uint16_t hid) {
    for (const auto &pair : kHidToEvdevTable) {
        if (pair.hid == hid) return pair.evdev;
    }
    return 0;
}

inline constexpr std::uint16_t evdevToHid(std::uint16_t evdev) {
    for (const auto &pair : kHidToEvdevTable) {
        if (pair.evdev == evdev) return pair.hid;
    }
    return 0;
}

// evdev 修饰键 → 协议位；非修饰键返回 0。
inline constexpr std::uint8_t modifierBitForEvdev(std::uint16_t evdev) {
    switch (evdev) {
    case KEY_LEFTCTRL: return kModLeftCtrl;
    case KEY_LEFTSHIFT: return kModLeftShift;
    case KEY_LEFTALT: return kModLeftAlt;
    case KEY_LEFTMETA: return kModLeftMeta;
    case KEY_RIGHTCTRL: return kModRightCtrl;
    case KEY_RIGHTSHIFT: return kModRightShift;
    case KEY_RIGHTALT: return kModRightAlt;
    case KEY_RIGHTMETA: return kModRightMeta;
    default: return 0;
    }
}

inline constexpr std::uint8_t modifierBitForHid(std::uint16_t hid) {
    return modifierBitForEvdev(hidToEvdev(hid));
}

// 协议 §6.4 的鼠标按钮号。命名出来是为了让「客户端镜像的协议常量」能被
// 黄金向量核对（protocol/testdata/input_vectors.json）——写成字面量就没法核对。
inline constexpr std::uint8_t kMouseButtonLeft   = 0x01;
inline constexpr std::uint8_t kMouseButtonRight  = 0x02;
inline constexpr std::uint8_t kMouseButtonMiddle = 0x03;
inline constexpr std::uint8_t kMouseButtonBack   = 0x04;
inline constexpr std::uint8_t kMouseButtonForward = 0x05;

// 协议鼠标按钮号（§6.4）→ evdev BTN_*；未知返回 0。
inline constexpr std::uint16_t mouseButtonToEvdev(std::uint8_t button) {
    switch (button) {
    case kMouseButtonLeft: return BTN_LEFT;
    case kMouseButtonRight: return BTN_RIGHT;
    case kMouseButtonMiddle: return BTN_MIDDLE;
    case kMouseButtonBack: return BTN_SIDE;
    case kMouseButtonForward: return BTN_EXTRA;
    default: return 0;
    }
}

inline constexpr std::uint8_t evdevToMouseButton(std::uint16_t code) {
    switch (code) {
    case BTN_LEFT: return 0x01;
    case BTN_RIGHT: return 0x02;
    case BTN_MIDDLE: return 0x03;
    case BTN_SIDE: return 0x04;
    case BTN_EXTRA: return 0x05;
    default: return 0;
    }
}

// 滚轮单位：协议 §5.3 写的是像素，但三端实现与 P11 结论都按「格/行」。
// evdev 的 REL_WHEEL/REL_HWHEEL 本来就是格，原样收发，不要乘 120。
inline constexpr int kScrollUnitLines = 1;

// 水平方向：协议正=向左（§5.3），evdev REL_HWHEEL 正=向右。
// 捕获与注入都要翻一次，否则 Linux↔Linux 的水平滚动会反向。
inline constexpr std::int32_t horizontalWheelSign(std::int32_t value) {
    return -value;
}

} // namespace fg::input

#endif // FLOWGRID_INPUT_KEYMAP_H
