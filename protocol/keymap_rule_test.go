package protocol

import (
	"testing"
)

func TestParseKeyMap(t *testing.T) {
	yaml := `
version: "1.0"
rules:
  - fromOS: macOS
    toOS: windows
    fromKey: 0x08
    toKey: 0x08
    modifiers: 8
    context: global
  - fromOS: macOS
    toOS: windows
    fromKey: 0x2B
    toKey: 0x2B
    modifiers: 2
    context: "app:com.microsoft.VSCode"
`
	kmf, err := ParseKeyMap([]byte(yaml))
	if err != nil {
		t.Fatal(err)
	}
	if kmf.Version != "1.0" {
		t.Errorf("Version: got %q, want %q", kmf.Version, "1.0")
	}
	if len(kmf.Rules) != 2 {
		t.Fatalf("Rules: got %d, want 2", len(kmf.Rules))
	}

	r := kmf.Rules[0]
	if r.FromOS != PlatformMacOS {
		t.Errorf("FromOS: got %v, want macOS", r.FromOS)
	}
	if r.ToOS != PlatformWindows {
		t.Errorf("ToOS: got %v, want Windows", r.ToOS)
	}
	if r.FromKey != 0x08 {
		t.Errorf("FromKey: got 0x%02X, want 0x08", r.FromKey)
	}
	if r.Context != ContextGlobal {
		t.Errorf("Context: got %q, want %q", r.Context, ContextGlobal)
	}

	r2 := kmf.Rules[1]
	if r2.Context != AppContext("com.microsoft.VSCode") {
		t.Errorf("Context: got %q, want %q", r2.Context, AppContext("com.microsoft.VSCode"))
	}
}

func TestKeyMapTableLookup(t *testing.T) {
	yaml := `
version: "1.0"
rules:
  - fromOS: macOS
    toOS: windows
    fromKey: 0x08
    toKey: 0x08
    modifiers: 8
    context: global
`
	kmf, err := ParseKeyMap([]byte(yaml))
	if err != nil {
		t.Fatal(err)
	}
	table := NewKeyMapTable(kmf)

	// Match
	key, mods, found := table.Lookup(PlatformMacOS, PlatformWindows, 0x08, 0, ContextGlobal)
	if !found {
		t.Error("expected to find rule")
	}
	if key != 0x08 || mods != 8 {
		t.Errorf("got key=0x%02X mods=0x%02X, want key=0x08 mods=0x08", key, mods)
	}

	// No match (wrong platform)
	key, mods, found = table.Lookup(PlatformLinux, PlatformWindows, 0x08, 0, ContextGlobal)
	if found {
		t.Error("should not find rule for Linux->Windows")
	}

	// No match (wrong key)
	key, _, found = table.Lookup(PlatformMacOS, PlatformWindows, 0x09, 0, ContextGlobal)
	if found {
		t.Error("should not find rule for key 0x09")
	}
}

func TestRemapModifierMaskHonorsKeymap(t *testing.T) {
	yaml := `
version: "1.0"
rules:
  - fromOS: windows
    toOS: macos
    fromKey: 0xE0
    toKey: 0xE3
    context: global
  - fromOS: windows
    toOS: macos
    fromKey: 0xE3
    toKey: 0xE2
    context: global
  - fromOS: windows
    toOS: macos
    fromKey: 0xE2
    toKey: 0xE0
    context: global
`
	kmf, err := ParseKeyMap([]byte(yaml))
	if err != nil {
		t.Fatal(err)
	}
	table := NewKeyMapTable(kmf)

	// 规则优先：Win -> Alt，而不是默认换算的 Ctrl。
	if got := table.RemapModifierMask(PlatformWindows, PlatformMacOS, ModLeftMeta, ContextGlobal); got != ModLeftAlt {
		t.Errorf("Win mask: got 0x%02X, want 0x%02X", got, ModLeftAlt)
	}
	if got := table.RemapModifierMask(PlatformWindows, PlatformMacOS, ModLeftCtrl, ContextGlobal); got != ModLeftMeta {
		t.Errorf("Ctrl mask: got 0x%02X, want 0x%02X", got, ModLeftMeta)
	}
	if got := table.RemapModifierMask(PlatformWindows, PlatformMacOS, ModLeftAlt, ContextGlobal); got != ModLeftCtrl {
		t.Errorf("Alt mask: got 0x%02X, want 0x%02X", got, ModLeftCtrl)
	}

	// 没有规则时回退平台默认换算：Ctrl <-> Cmd。
	empty := NewKeyMapTable(&KeyMapFile{})
	if got := empty.RemapModifierMask(PlatformWindows, PlatformMacOS, ModLeftCtrl|ModLeftShift, ContextGlobal); got != ModLeftMeta|ModLeftShift {
		t.Errorf("fallback: got 0x%02X, want 0x%02X", got, ModLeftMeta|ModLeftShift)
	}
	// nil table 与同平台直通都不应崩。
	var nilTable *KeyMapTable
	if got := nilTable.RemapModifierMask(PlatformMacOS, PlatformWindows, ModLeftMeta, ContextGlobal); got != ModLeftCtrl {
		t.Errorf("nil table: got 0x%02X, want 0x%02X", got, ModLeftCtrl)
	}
	if got := table.RemapModifierMask(PlatformWindows, PlatformWindows, ModLeftAlt, ContextGlobal); got != ModLeftAlt {
		t.Errorf("same platform: got 0x%02X, want 0x%02X", got, ModLeftAlt)
	}
}

func TestParseKeyMapDefaultVersion(t *testing.T) {
	yaml := `rules: []`
	kmf, err := ParseKeyMap([]byte(yaml))
	if err != nil {
		t.Fatal(err)
	}
	if kmf.Version != KeyMapVersion {
		t.Errorf("Version: got %q, want %q", kmf.Version, KeyMapVersion)
	}
}
