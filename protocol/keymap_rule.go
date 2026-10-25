package protocol

import (
	"fmt"
	"os"
	"strings"

	"gopkg.in/yaml.v3"
)

// KeyMapContext identifies the scope of a key mapping rule (§11.2).
type KeyMapContext string

const (
	ContextGlobal KeyMapContext = "global"
	ContextGame   KeyMapContext = "game"
)

// AppContext returns an app-specific context string.
func AppContext(bundleID string) KeyMapContext {
	return KeyMapContext("app:" + bundleID)
}

// KeyMapRule represents a single key mapping rule (§11.1).
type KeyMapRule struct {
	FromOS    Platform      `yaml:"fromOS"`
	ToOS      Platform      `yaml:"toOS"`
	FromKey   uint16        `yaml:"fromKey"`   // HID Usage ID
	ToKey     uint16        `yaml:"toKey"`     // HID Usage ID
	Modifiers uint8         `yaml:"modifiers"` // modifier bitmask
	Context   KeyMapContext `yaml:"context"`
}

// KeyMapFile is the top-level YAML structure for keymap files (§11.1).
type KeyMapFile struct {
	Version string       `yaml:"version"`
	Rules   []KeyMapRule `yaml:"rules"`
}

// platformFromString converts a YAML string to Platform.
// Input is normalized to lowercase before matching.
func platformFromString(s string) (Platform, error) {
	switch strings.ToLower(s) {
	case "macos", "mac":
		return PlatformMacOS, nil
	case "windows", "win":
		return PlatformWindows, nil
	case "linux":
		return PlatformLinux, nil
	case "android":
		return PlatformAndroid, nil
	case "ios", "ipados":
		return PlatformIOS, nil
	default:
		return 0, fmt.Errorf("unknown platform: %s", s)
	}
}

// UnmarshalYAML implements custom unmarshalling for KeyMapRule to handle
// platform strings and hex key codes.
func (r *KeyMapRule) UnmarshalYAML(value *yaml.Node) error {
	var raw struct {
		FromOS    string `yaml:"fromOS"`
		ToOS      string `yaml:"toOS"`
		FromKey   uint16 `yaml:"fromKey"`
		ToKey     uint16 `yaml:"toKey"`
		Modifiers uint8  `yaml:"modifiers"`
		Context   string `yaml:"context"`
	}
	if err := value.Decode(&raw); err != nil {
		return err
	}

	fromOS, err := platformFromString(raw.FromOS)
	if err != nil {
		return err
	}
	toOS, err := platformFromString(raw.ToOS)
	if err != nil {
		return err
	}

	r.FromOS = fromOS
	r.ToOS = toOS
	r.FromKey = raw.FromKey
	r.ToKey = raw.ToKey
	r.Modifiers = raw.Modifiers
	if raw.Context == "" {
		r.Context = ContextGlobal
	} else {
		r.Context = KeyMapContext(raw.Context)
	}
	return nil
}

// LoadKeyMapFile reads and parses a YAML keymap file.
func LoadKeyMapFile(path string) (*KeyMapFile, error) {
	data, err := os.ReadFile(path)
	if err != nil {
		return nil, fmt.Errorf("read keymap file: %w", err)
	}
	return ParseKeyMap(data)
}

// ParseKeyMap parses YAML keymap data.
// A non-empty version must match the supported KeyMapVersion exactly (§11.3
// step 3: validate before loading — silently adopting a foreign version's
// rule semantics would misapply mappings).
func ParseKeyMap(data []byte) (*KeyMapFile, error) {
	var kmf KeyMapFile
	if err := yaml.Unmarshal(data, &kmf); err != nil {
		return nil, fmt.Errorf("parse keymap: %w", err)
	}
	if kmf.Version == "" {
		kmf.Version = KeyMapVersion
	} else if kmf.Version != KeyMapVersion {
		return nil, NewError(ErrVersionMismatch, fmt.Sprintf("keymap version %q, want %q", kmf.Version, KeyMapVersion))
	}
	return &kmf, nil
}

// KeyMapTable is an immutable lookup structure for efficient key remapping.
// After construction via NewKeyMapTable, the table should not be modified.
// For hot-reload (§11.3), replace the entire table pointer atomically.
type KeyMapTable struct {
	rules []KeyMapRule
}

// NewKeyMapTable builds a lookup table from a KeyMapFile.
func NewKeyMapTable(kmf *KeyMapFile) *KeyMapTable {
	t := &KeyMapTable{
		rules: make([]KeyMapRule, len(kmf.Rules)),
	}
	copy(t.rules, kmf.Rules)
	return t
}

// RuleCount 返回规则条数。
//
// 为什么规则本体在协议层却还要一个「列出」的入口：界面要显示**真实的规则
// 表**（哪一条、什么方向、什么键），而不是只回答「查某个键会变成什么」。
// 规则文件是 YAML，解析在协议层；客户端自己再解析一遍就成了第二份实现。
func (t *KeyMapTable) RuleCount() int {
	if t == nil {
		return 0
	}
	return len(t.rules)
}

// RuleAt 返回第 index 条规则。越界返回 false。
func (t *KeyMapTable) RuleAt(index int) (KeyMapRule, bool) {
	if t == nil || index < 0 || index >= len(t.rules) {
		return KeyMapRule{}, false
	}
	return t.rules[index], true
}

// KeyMapConflict 描述一条「会触发目标平台系统快捷键」的规则。
type KeyMapConflict struct {
	FromOS  Platform
	FromKey uint16
	ToOS    Platform
	ToKey   uint16
}

// IsSystemModifierKey 报告 key 在 platform 上是不是「系统修饰键」。
//
// 系统修饰键 = 目标系统的全局快捷键占用的那个修饰键：
//   - macOS / iOS：⌘（Cmd+Tab 切应用、Cmd+空格开聚焦）与 ⌥（Option+Tab 切窗口）
//   - Linux：Super（桌面环境快捷键）
//   - Windows / Android：Win
//
// 为什么要单列这个判定：键位规则是**整键**映射——把某个键映到系统修饰键
// 上，目标机器上「修饰键 + 其它键」的组合就可能被系统截走（Linux 的
// Ctrl+Tab 到 macOS 变成 Cmd+Tab = 切应用，而不是在应用里切标签页）。
// 界面据此给出冲突提示，所以「哪个键在哪个平台是系统键」这个知识必须在
// 协议层——客户端自己判断一遍就成了第二份平台语义。
func IsSystemModifierKey(platform Platform, key uint16) bool {
	switch platform {
	case PlatformMacOS, PlatformIOS:
		switch key {
		case HIDKeyLMeta, HIDKeyRMeta, HIDKeyLAlt, HIDKeyRAlt:
			return true
		}
	case PlatformLinux, PlatformWindows, PlatformAndroid:
		switch key {
		case HIDKeyLMeta, HIDKeyRMeta:
			return true
		}
	}
	return false
}

// Conflicts 返回规则表里「会触发目标平台系统快捷键」的规则。
//
// 只看全局规则：应用级覆盖（app:/game 上下文）本来就是给某个应用抢这些
// 组合键用的，那不算冲突。
func (t *KeyMapTable) Conflicts() []KeyMapConflict {
	if t == nil {
		return nil
	}
	var out []KeyMapConflict
	for _, rule := range t.rules {
		if rule.Context != ContextGlobal {
			continue
		}
		if !IsSystemModifierKey(rule.ToOS, rule.ToKey) {
			continue
		}
		out = append(out, KeyMapConflict{
			FromOS:  rule.FromOS,
			FromKey: rule.FromKey,
			ToOS:    rule.ToOS,
			ToKey:   rule.ToKey,
		})
	}
	return out
}

// modifierKeyBits pairs each protocol modifier bit with its HID Usage ID.
var modifierKeyBits = [...]struct {
	bit uint8
	hid uint16
}{
	{ModLeftCtrl, HIDKeyLCtrl},
	{ModLeftShift, HIDKeyLShift},
	{ModLeftAlt, HIDKeyLAlt},
	{ModLeftMeta, HIDKeyLMeta},
	{ModRightCtrl, HIDKeyRCtrl},
	{ModRightShift, HIDKeyRShift},
	{ModRightAlt, HIDKeyRAlt},
	{ModRightMeta, HIDKeyRMeta},
}

// RemapModifierMask converts a source modifier bitmask for the destination
// platform, honoring explicit keymap rules for modifier keys first and falling
// back to RemapModifierKey for keys without a rule.
//
// RemapModifiers only knows the platform default (Ctrl <-> Meta). A user keymap
// can say e.g. Windows Win -> macOS Alt; using the default conversion for the
// mask would then produce Ctrl while the KEY_DOWN frames carry Alt. This method
// keeps the mask and the frames consistent. context is the same app/game
// context passed to Lookup.
func (t *KeyMapTable) RemapModifierMask(from, to Platform, mods uint8, context KeyMapContext) uint8 {
	if from == to {
		return mods
	}
	var out uint8
	for _, m := range modifierKeyBits {
		if mods&m.bit == 0 {
			continue
		}
		hid := m.hid
		if t != nil {
			if nk, _, ok := t.Lookup(from, to, m.hid, mods, context); ok {
				hid = nk
			} else {
				hid = RemapModifierKey(m.hid, from, to)
			}
		} else {
			hid = RemapModifierKey(m.hid, from, to)
		}
		if bit := ModifierFromSingleKey(hid); bit != 0 {
			out |= bit
		}
	}
	return out
}

// Lookup finds a matching rule for the given parameters.
// Precedence: context-specific rules (app/game) > global rules.
// Returns the remapped key code, modifiers, and true if a rule matched.
// If no rule matches, returns the original key and modifiers unchanged.
func (t *KeyMapTable) Lookup(fromOS, toOS Platform, keyCode uint16, modifiers uint8, context KeyMapContext) (uint16, uint8, bool) {
	// First pass: look for context-specific rules (app or game)
	for _, rule := range t.rules {
		if rule.FromOS != fromOS || rule.ToOS != toOS {
			continue
		}
		if rule.FromKey != keyCode {
			continue
		}
		if rule.Context == context {
			return rule.ToKey, rule.Modifiers, true
		}
	}
	// Second pass: look for global rules
	for _, rule := range t.rules {
		if rule.FromOS != fromOS || rule.ToOS != toOS {
			continue
		}
		if rule.FromKey != keyCode {
			continue
		}
		if rule.Context == ContextGlobal {
			return rule.ToKey, rule.Modifiers, true
		}
	}
	return keyCode, modifiers, false
}
