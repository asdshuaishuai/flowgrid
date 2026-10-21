package wininput

import (
	"testing"

	"github.com/flowgrid/protocol"
)

func TestHIDToVKFixesKnownWrongKeys(t *testing.T) {
	cases := []struct {
		name string
		hid  uint16
		want uint16
	}{
		{"PrintScreen", protocol.HIDKeyPrintScreen, vkSnapshot},
		{"KpDot", protocol.HIDKeyKpDot, vkDecimal},
		{"KpEnter", protocol.HIDKeyKpEnter, vkReturn},
		{"Enter", protocol.HIDKeyEnter, vkReturn},
		{"Delete", protocol.HIDKeyDelete, vkDelete},
		{"Backslash", 0x31, vkOemBackslash},
	}
	for _, c := range cases {
		got, ok := HIDToVK(c.hid)
		if !ok || got != c.want {
			t.Errorf("%s: HID 0x%02X → VK 0x%02X (ok=%v), want 0x%02X", c.name, c.hid, got, ok, c.want)
		}
	}
}

// 反查表必须确定性：历史上遍历 map 会让 Enter/Delete 随机落到小键盘上。
func TestVKToHIDIsDeterministic(t *testing.T) {
	want := map[uint16]uint16{
		vkReturn:       protocol.HIDKeyEnter,
		vkDelete:       protocol.HIDKeyDelete,
		vkOem102:       0x64,
		vkOemBackslash: 0x31,
	}
	for i := 0; i < 100; i++ {
		for vk, hid := range want {
			got, ok := VKToHID(vk)
			if !ok || got != hid {
				t.Fatalf("VKToHID(0x%02X) = 0x%02X (ok=%v), want 0x%02X", vk, got, ok, hid)
			}
		}
	}
}

func TestExtendedKeys(t *testing.T) {
	if !IsExtendedVK(vkSnapshot) {
		t.Error("PrintScreen needs KEYEVENTF_EXTENDEDKEY (otherwise it is Numpad *)")
	}
	if !IsExtendedVK(vkNumLock) {
		t.Error("NumLock needs KEYEVENTF_EXTENDEDKEY (otherwise it is Pause)")
	}
	if !IsExtendedVK(vkLWin) {
		t.Error("Left Win needs KEYEVENTF_EXTENDEDKEY")
	}
	if IsExtendedVK(vkReturn) {
		t.Error("main Enter must not be extended")
	}
	if !IsKeypadEnter(protocol.HIDKeyKpEnter) || IsKeypadEnter(protocol.HIDKeyEnter) {
		t.Error("only HIDKeyKpEnter should carry the extended flag")
	}
}

func TestWheelLines(t *testing.T) {
	var rem int32
	// 一格 = 120 → 1；两个方向都要对。
	if got := wheelLines(120, &rem); got != 1 || rem != 0 {
		t.Fatalf("120 → %d (rem %d), want 1/0", got, rem)
	}
	if got := wheelLines(-240, &rem); got != -2 || rem != 0 {
		t.Fatalf("-240 → %d (rem %d), want -2/0", got, rem)
	}
	// 高精度滚轮：40+40+40 才凑够一格，前两次不能丢余数。
	for i := 0; i < 2; i++ {
		if got := wheelLines(40, &rem); got != 0 {
			t.Fatalf("partial %d → %d, want 0", i, got)
		}
	}
	if rem != 80 {
		t.Fatalf("remainder after two 40s = %d, want 80", rem)
	}
	if got := wheelLines(40, &rem); got != 1 || rem != 0 {
		t.Fatalf("third 40 → %d (rem %d), want 1/0", got, rem)
	}
	// 反向余数不能丢。
	if got := wheelLines(-40, &rem); got != 0 || rem != -40 {
		t.Fatalf("-40 → %d (rem %d), want 0/-40", got, rem)
	}
	if got := wheelLines(-80, &rem); got != -1 || rem != 0 {
		t.Fatalf("-80 more → %d (rem %d), want -1/0", got, rem)
	}
}

func TestHorizontalWheelDirection(t *testing.T) {
	// 协议正=向左；Windows WM_MOUSEHWHEEL 正=向右。
	if got := int32(horizontalWheelData(1)); got != -120 {
		t.Fatalf("protocol left (+1) -> Windows %d, want -120", got)
	}
	if got := int32(horizontalWheelData(-2)); got != 240 {
		t.Fatalf("protocol right (-2) -> Windows %d, want 240", got)
	}
}

func TestModifierChanges(t *testing.T) {
	press, release := modifierChanges(0, protocol.ModLeftCtrl|protocol.ModLeftShift)
	if len(press) != 2 || len(release) != 0 {
		t.Fatalf("press=%v release=%v, want two presses", press, release)
	}
	if press[0] != protocol.HIDKeyLCtrl || press[1] != protocol.HIDKeyLShift {
		t.Fatalf("press order = %v", press)
	}

	// 目标只按 Ctrl，但发送端报告 Alt：松开 Ctrl、补按 Alt。
	press, release = modifierChanges(protocol.ModLeftCtrl, protocol.ModLeftAlt)
	if len(press) != 1 || press[0] != protocol.HIDKeyLAlt {
		t.Fatalf("press = %v, want LAlt", press)
	}
	if len(release) != 1 || release[0] != protocol.HIDKeyLCtrl {
		t.Fatalf("release = %v, want LCtrl", release)
	}

	// 状态已一致时不产生任何动作（周期重复的 KEY_DOWN 不应反复按修饰键）。
	press, release = modifierChanges(protocol.ModLeftShift, protocol.ModLeftShift)
	if len(press) != 0 || len(release) != 0 {
		t.Fatalf("no-op sync produced press=%v release=%v", press, release)
	}
}
