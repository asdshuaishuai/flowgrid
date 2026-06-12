package protocol

import (
	"fmt"
	"os"

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
	FromOS    Platform     `yaml:"fromOS"`
	ToOS      Platform     `yaml:"toOS"`
	FromKey   uint16       `yaml:"fromKey"`   // HID Usage ID
	ToKey     uint16       `yaml:"toKey"`     // HID Usage ID
	Modifiers uint8        `yaml:"modifiers"` // modifier bitmask
	Context   KeyMapContext `yaml:"context"`
}

// KeyMapFile is the top-level YAML structure for keymap files (§11.1).
type KeyMapFile struct {
	Version string      `yaml:"version"`
	Rules   []KeyMapRule `yaml:"rules"`
}

// platformFromString converts a YAML string to Platform.
func platformFromString(s string) (Platform, error) {
	switch s {
	case "macos", "macOS", "mac":
		return PlatformMacOS, nil
	case "windows", "Windows", "win":
		return PlatformWindows, nil
	case "linux", "Linux":
		return PlatformLinux, nil
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
func ParseKeyMap(data []byte) (*KeyMapFile, error) {
	var kmf KeyMapFile
	if err := yaml.Unmarshal(data, &kmf); err != nil {
		return nil, fmt.Errorf("parse keymap: %w", err)
	}
	if kmf.Version == "" {
		kmf.Version = KeyMapVersion
	}
	return &kmf, nil
}

// KeyMapTable is a lookup structure for efficient key remapping.
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

// Lookup finds a matching rule for the given parameters.
// Returns the remapped key code, modifiers, and true if a rule matched.
// If no rule matches, returns the original key and modifiers unchanged.
func (t *KeyMapTable) Lookup(fromOS, toOS Platform, keyCode uint16, modifiers uint8, context KeyMapContext) (uint16, uint8, bool) {
	for _, rule := range t.rules {
		if rule.FromOS != fromOS || rule.ToOS != toOS {
			continue
		}
		if rule.FromKey != keyCode {
			continue
		}
		if rule.Context != ContextGlobal && rule.Context != context {
			continue
		}
		return rule.ToKey, rule.Modifiers, true
	}
	return keyCode, modifiers, false
}

// Rules returns all rules in the table.
func (t *KeyMapTable) Rules() []KeyMapRule {
	return t.rules
}
