// Linux 键鼠映射表的独立单测：不需要 Qt/DTK，也不需要 /dev/uinput。
// 构建运行（任意 Linux，见 deepin/README.md「键鼠软模拟」一节）：
//   g++ -std=c++17 -Wall -Wextra -Werror -Isrc/core/input tests/input_keymap_test.cpp -o /tmp/input_keymap_test
//   /tmp/input_keymap_test
#include "input_keymap.h"

#include <cstdio>
#include <cstdlib>
#include <set>

namespace {

int failures = 0;

void check(bool condition, const char *what) {
    if (condition) return;
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
}

} // namespace

int main() {
    using namespace fg::input;

    // 整张表双向可逆，且没有重复键码。
    std::set<std::uint16_t> hids;
    std::set<std::uint16_t> codes;
    for (const auto &pair : kHidToEvdevTable) {
        check(hids.insert(pair.hid).second, "duplicate HID in table");
        check(codes.insert(pair.evdev).second, "duplicate evdev code in table");
        check(hidToEvdev(pair.hid) == pair.evdev, "hidToEvdev roundtrip");
        check(evdevToHid(pair.evdev) == pair.hid, "evdevToHid roundtrip");
    }

    // 抽查容易写错的几个：字母、回车、反斜杠、102 键、菜单键、修饰键。
    check(hidToEvdev(0x04) == KEY_A, "HID A -> KEY_A");
    check(hidToEvdev(0x28) == KEY_ENTER, "HID Enter -> KEY_ENTER");
    check(hidToEvdev(0x31) == KEY_BACKSLASH, "HID backslash -> KEY_BACKSLASH");
    check(hidToEvdev(0x64) == KEY_102ND, "HID 102nd -> KEY_102ND");
    check(hidToEvdev(0x65) == KEY_COMPOSE, "HID menu -> KEY_COMPOSE");
    check(hidToEvdev(0xE3) == KEY_LEFTMETA, "HID LMeta -> KEY_LEFTMETA");
    check(hidToEvdev(0xE7) == KEY_RIGHTMETA, "HID RMeta -> KEY_RIGHTMETA");
    check(hidToEvdev(0x99) == 0, "unknown HID is unmapped");

    // 修饰位：左右必须分开，非修饰键必须是 0。
    check(modifierBitForEvdev(KEY_LEFTCTRL) == kModLeftCtrl, "left ctrl bit");
    check(modifierBitForEvdev(KEY_RIGHTMETA) == kModRightMeta, "right meta bit");
    check(modifierBitForEvdev(KEY_A) == 0, "letter is not modifier");
    check(modifierBitForHid(0xE1) == kModLeftShift, "HID left shift bit");
    check(modifierBitForHid(0x04) == 0, "HID A is not modifier");

    // 鼠标按钮：协议 4/5 是后退/前进，不能都当中键。
    check(mouseButtonToEvdev(0x01) == BTN_LEFT, "button 1 -> BTN_LEFT");
    check(mouseButtonToEvdev(0x03) == BTN_MIDDLE, "button 3 -> BTN_MIDDLE");
    check(mouseButtonToEvdev(0x04) == BTN_SIDE, "button 4 -> BTN_SIDE");
    check(mouseButtonToEvdev(0x05) == BTN_EXTRA, "button 5 -> BTN_EXTRA");
    check(mouseButtonToEvdev(0x09) == 0, "unknown button unmapped");
    check(evdevToMouseButton(BTN_SIDE) == 0x04, "BTN_SIDE -> button 4");
    check(evdevToMouseButton(BTN_EXTRA) == 0x05, "BTN_EXTRA -> button 5");
    check(evdevToMouseButton(KEY_A) == 0, "key is not mouse button");

    // 滚轮单位是「格/行」：evdev 原样收，注入原样写（P11）。
    check(kScrollUnitLines == 1, "one scroll unit is one line");
    // 水平方向：协议正=向左，evdev 正=向右。
    check(horizontalWheelSign(1) == -1, "protocol left -> evdev negative");
    check(horizontalWheelSign(-2) == 2, "protocol right -> evdev positive");

    if (failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("input_keymap_test: %zu mappings OK\n",
                sizeof(kHidToEvdevTable) / sizeof(kHidToEvdevTable[0]));
    return EXIT_SUCCESS;
}
