package protocol

import "testing"

func TestDecodeInputFrameKeyDownRemap(t *testing.T) {
	// Cmd+C on macOS → Ctrl+C on Linux, no keymap table (platform default).
	payload := (&KeyDownPayload{KeyCode: HIDKeyC, Modifiers: ModLeftMeta}).Marshal()
	ev, err := DecodeInputFrame(FrameKeyDown, payload, PlatformMacOS, PlatformLinux, nil, ContextGlobal)
	if err != nil {
		t.Fatalf("DecodeInputFrame: %v", err)
	}
	if ev.KeyCode != HIDKeyC {
		t.Errorf("KeyCode: got 0x%04X, want 0x%04X (HIDKeyC)", ev.KeyCode, HIDKeyC)
	}
	if ev.Modifiers != ModLeftCtrl {
		t.Errorf("Modifiers: got 0x%02X, want 0x%02X (ModLeftCtrl, Meta→Ctrl)", ev.Modifiers, ModLeftCtrl)
	}
	if !ev.Remapped {
		t.Error("Remapped: got false, want true")
	}
	if ev.OrigKeyCode != HIDKeyC || ev.OrigModifiers != ModLeftMeta {
		t.Errorf("Orig: got 0x%04X/0x%02X, want 0x%04X/0x%02X", ev.OrigKeyCode, ev.OrigModifiers, HIDKeyC, ModLeftMeta)
	}
}

func TestDecodeInputFrameSamePlatformNoRemap(t *testing.T) {
	payload := (&KeyDownPayload{KeyCode: HIDKeyC, Modifiers: ModLeftMeta}).Marshal()
	ev, err := DecodeInputFrame(FrameKeyDown, payload, PlatformMacOS, PlatformMacOS, nil, ContextGlobal)
	if err != nil {
		t.Fatalf("DecodeInputFrame: %v", err)
	}
	if ev.KeyCode != HIDKeyC || ev.Modifiers != ModLeftMeta || ev.Remapped {
		t.Errorf("same platform should pass through: got 0x%04X/0x%02X remapped=%v", ev.KeyCode, ev.Modifiers, ev.Remapped)
	}
}

func TestDecodeInputFrameKeyUpModifierKey(t *testing.T) {
	// Releasing Left-Meta on macOS arrives as Left-Ctrl on Linux (§6.3).
	payload := (&KeyUpPayload{KeyCode: HIDKeyLMeta}).Marshal()
	ev, err := DecodeInputFrame(FrameKeyUp, payload, PlatformMacOS, PlatformLinux, nil, ContextGlobal)
	if err != nil {
		t.Fatalf("DecodeInputFrame: %v", err)
	}
	if ev.KeyCode != HIDKeyLCtrl {
		t.Errorf("KeyCode: got 0x%04X, want 0x%04X (HIDKeyLCtrl)", ev.KeyCode, HIDKeyLCtrl)
	}
	if ev.Modifiers != 0 {
		t.Errorf("Modifiers: got 0x%02X, want 0 (KEY_UP carries no mask)", ev.Modifiers)
	}
}

func TestDecodeInputFrameMouseAndScroll(t *testing.T) {
	move, err := DecodeInputFrame(FrameMouseMove, (&MouseMovePayload{DeltaX: 120, DeltaY: -45}).Marshal(),
		PlatformMacOS, PlatformLinux, nil, ContextGlobal)
	if err != nil {
		t.Fatalf("move: %v", err)
	}
	if move.DeltaX != 120 || move.DeltaY != -45 {
		t.Errorf("move delta: got %d/%d, want 120/-45", move.DeltaX, move.DeltaY)
	}

	btn, err := DecodeInputFrame(FrameMouseButton, (&MouseButtonPayload{ButtonID: MouseButtonLeft, State: 0x01}).Marshal(),
		PlatformMacOS, PlatformLinux, nil, ContextGlobal)
	if err != nil {
		t.Fatalf("button: %v", err)
	}
	if btn.Button != MouseButtonLeft || !btn.Pressed {
		t.Errorf("button: got id=%d pressed=%v, want id=%d pressed=true", btn.Button, btn.Pressed, MouseButtonLeft)
	}

	rel, err := DecodeInputFrame(FrameMouseButton, (&MouseButtonPayload{ButtonID: MouseButtonLeft, State: 0x00}).Marshal(),
		PlatformMacOS, PlatformLinux, nil, ContextGlobal)
	if err != nil {
		t.Fatalf("button release: %v", err)
	}
	if rel.Pressed {
		t.Error("button release: got pressed=true, want false")
	}

	scr, err := DecodeInputFrame(FrameScroll, (&ScrollPayload{DeltaY: 3, DeltaX: 0}).Marshal(),
		PlatformMacOS, PlatformLinux, nil, ContextGlobal)
	if err != nil {
		t.Fatalf("scroll: %v", err)
	}
	if scr.DeltaY != 3 || scr.DeltaX != 0 {
		t.Errorf("scroll delta: got %d/%d, want 3/0", scr.DeltaY, scr.DeltaX)
	}
}

func TestDecodeInputFrameKeyMapRuleOverride(t *testing.T) {
	// Explicit rule beats the platform default: macOS C (with Cmd) → Linux V+Ctrl.
	table := NewKeyMapTable(&KeyMapFile{Rules: []KeyMapRule{
		{FromOS: PlatformMacOS, ToOS: PlatformLinux, FromKey: HIDKeyC, ToKey: HIDKeyV, Modifiers: ModLeftCtrl, Context: ContextGlobal},
	}})
	payload := (&KeyDownPayload{KeyCode: HIDKeyC, Modifiers: ModLeftMeta}).Marshal()
	ev, err := DecodeInputFrame(FrameKeyDown, payload, PlatformMacOS, PlatformLinux, table, ContextGlobal)
	if err != nil {
		t.Fatalf("DecodeInputFrame: %v", err)
	}
	if ev.KeyCode != HIDKeyV || ev.Modifiers != ModLeftCtrl {
		t.Errorf("rule override: got 0x%04X/0x%02X, want 0x%04X/0x%02X", ev.KeyCode, ev.Modifiers, HIDKeyV, ModLeftCtrl)
	}
}

func TestDecodeInputFrameErrors(t *testing.T) {
	if _, err := DecodeInputFrame(FrameKeyDown, []byte{0x00}, PlatformMacOS, PlatformLinux, nil, ContextGlobal); err == nil {
		t.Error("short KEY_DOWN payload: want error, got nil")
	}
	if _, err := DecodeInputFrame(FrameIdentify, []byte{1, 2, 3}, PlatformMacOS, PlatformLinux, nil, ContextGlobal); err == nil {
		t.Error("non-input frame type: want error, got nil")
	}
}

func TestInputEventDescribe(t *testing.T) {
	payload := (&KeyDownPayload{KeyCode: HIDKeyC, Modifiers: ModLeftMeta}).Marshal()
	ev, _ := DecodeInputFrame(FrameKeyDown, payload, PlatformMacOS, PlatformLinux, nil, ContextGlobal)
	if got := ev.Describe(); got == "" {
		t.Fatal("Describe returned empty")
	}
	// Should mention the keydown and the cross-platform remap.
	for _, want := range []string{"keydown", "macOS", "Linux"} {
		if !contains(ev.Describe(), want) {
			t.Errorf("Describe %q missing %q", ev.Describe(), want)
		}
	}
	move, _ := DecodeInputFrame(FrameMouseMove, (&MouseMovePayload{DeltaX: 5, DeltaY: 6}).Marshal(),
		PlatformMacOS, PlatformLinux, nil, ContextGlobal)
	if got := move.Describe(); !contains(got, "mousemove") {
		t.Errorf("move Describe %q missing mousemove", got)
	}
}

func contains(s, sub string) bool {
	for i := 0; i+len(sub) <= len(s); i++ {
		if s[i:i+len(sub)] == sub {
			return true
		}
	}
	return false
}
