package protocol

import (
	"testing"
)

func TestRemapModifiersMacToWin(t *testing.T) {
	tests := []struct {
		name string
		mods uint8
		want uint8
	}{
		{"left meta -> left ctrl", ModLeftMeta, ModLeftCtrl},
		{"right meta -> right ctrl", ModRightMeta, ModRightCtrl},
		{"shift unchanged", ModLeftShift, ModLeftShift},
		{"alt unchanged", ModLeftAlt, ModLeftAlt},
		{"cmd+shift -> ctrl+shift", ModLeftMeta | ModLeftShift, ModLeftCtrl | ModLeftShift},
		{"cmd+c -> ctrl+c", ModLeftMeta, ModLeftCtrl},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			got := RemapModifiers(tt.mods, PlatformMacOS, PlatformWindows)
			if got != tt.want {
				t.Errorf("got 0x%02X, want 0x%02X", got, tt.want)
			}
		})
	}
}

func TestRemapModifiersWinToMac(t *testing.T) {
	tests := []struct {
		name string
		mods uint8
		want uint8
	}{
		{"left ctrl -> left meta", ModLeftCtrl, ModLeftMeta},
		{"right ctrl -> right meta", ModRightCtrl, ModRightMeta},
		{"shift unchanged", ModRightShift, ModRightShift},
		{"ctrl+c -> cmd+c", ModLeftCtrl, ModLeftMeta},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			got := RemapModifiers(tt.mods, PlatformWindows, PlatformMacOS)
			if got != tt.want {
				t.Errorf("got 0x%02X, want 0x%02X", got, tt.want)
			}
		})
	}
}

func TestRemapModifiersWinLinux(t *testing.T) {
	// Windows <-> Linux: no remapping
	mods := ModLeftCtrl | ModLeftShift | ModLeftAlt
	got := RemapModifiers(mods, PlatformWindows, PlatformLinux)
	if got != mods {
		t.Errorf("expected no change, got 0x%02X want 0x%02X", got, mods)
	}
}

func TestRemapModifiersSamePlatform(t *testing.T) {
	mods := ModLeftMeta | ModLeftCtrl
	got := RemapModifiers(mods, PlatformMacOS, PlatformMacOS)
	if got != mods {
		t.Errorf("same platform should not remap, got 0x%02X want 0x%02X", got, mods)
	}
}

func TestModifierFromSingleKey(t *testing.T) {
	tests := []struct {
		key  uint16
		want uint8
	}{
		{0xE0, ModLeftCtrl},
		{0xE1, ModLeftShift},
		{0xE2, ModLeftAlt},
		{0xE3, ModLeftMeta},
		{0xE4, ModRightCtrl},
		{0xE5, ModRightShift},
		{0xE6, ModRightAlt},
		{0xE7, ModRightMeta},
		{HIDKeyA, 0}, // not a modifier
	}
	for _, tt := range tests {
		got := ModifierFromSingleKey(tt.key)
		if got != tt.want {
			t.Errorf("ModifierFromSingleKey(0x%02X): got 0x%02X, want 0x%02X", tt.key, got, tt.want)
		}
	}
}

func TestPlatformString(t *testing.T) {
	if PlatformMacOS.String() != "macOS" {
		t.Errorf("got %q, want %q", PlatformMacOS.String(), "macOS")
	}
	if PlatformWindows.String() != "Windows" {
		t.Errorf("got %q, want %q", PlatformWindows.String(), "Windows")
	}
	if Platform(0xFF).String() != "Unknown" {
		t.Errorf("got %q, want %q", Platform(0xFF).String(), "Unknown")
	}
}
