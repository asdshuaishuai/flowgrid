//! In-memory rule table, YAML loading, and lookup by (from_os, to_os, key, context).

use crate::device::Platform;
use crate::error::{FlowGridError, Result};
use serde::{Deserialize, Serialize};
use std::collections::HashMap;
use std::path::Path;
use std::sync::{Arc, RwLock};
use tracing::{debug, info, warn};

/// Context values for a mapping rule.
#[derive(Debug, Clone, PartialEq, Eq, Hash, Serialize)]
#[derive(Default)]
pub enum RuleContext {
    #[serde(rename = "global")]
    #[default]
    Global,
    #[serde(rename = "app")]
    App(String),
    #[serde(rename = "game")]
    Game,
}

impl<'de> Deserialize<'de> for RuleContext {
    fn deserialize<D>(deserializer: D) -> std::result::Result<Self, D::Error>
    where
        D: serde::Deserializer<'de>,
    {
        use serde::de::Error;
        let value = serde_yaml::Value::deserialize(deserializer)?;
        match value {
            serde_yaml::Value::String(s) => {
                if s == "global" {
                    Ok(RuleContext::Global)
                } else if s == "game" {
                    Ok(RuleContext::Game)
                } else if let Some(app) = s.strip_prefix("app:") {
                    Ok(RuleContext::App(app.to_string()))
                } else {
                    Err(D::Error::custom(format!("unknown context string: {s}")))
                }
            }
            serde_yaml::Value::Mapping(mut m) => {
                if let Some((_, serde_yaml::Value::String(v))) = m.remove_entry(serde_yaml::Value::String("app".to_string())) {
                    Ok(RuleContext::App(v))
                } else if m.contains_key(serde_yaml::Value::String("global".to_string())) || m.is_empty() {
                    Ok(RuleContext::Global)
                } else if m.contains_key(serde_yaml::Value::String("game".to_string())) {
                    Ok(RuleContext::Game)
                } else {
                    Err(D::Error::custom("unknown context mapping"))
                }
            }
            _ => Err(D::Error::custom("expected string or mapping for context")),
        }
    }
}


impl RuleContext {
    pub fn as_str(&self) -> String {
        match self {
            RuleContext::Global => "global".to_string(),
            RuleContext::App(bundle) => format!("app:{bundle}"),
            RuleContext::Game => "game".to_string(),
        }
    }

    pub fn parse(value: &str) -> Self {
        if value == "global" {
            Self::Global
        } else if value == "game" {
            Self::Game
        } else if let Some(app) = value.strip_prefix("app:") {
            Self::App(app.to_string())
        } else {
            Self::Global
        }
    }
}

/// A single key-mapping rule loaded from YAML.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct MappingRule {
    #[serde(rename = "fromOS")]
    pub from_os: String,
    #[serde(rename = "toOS")]
    pub to_os: String,
    #[serde(rename = "fromKey")]
    pub from_key: u16,
    #[serde(rename = "toKey")]
    pub to_key: u16,
    pub modifiers: u8,
    pub context: RuleContext,
}

/// Parsed and validated platform representation for a rule.
#[derive(Debug, Clone, PartialEq, Eq, Hash)]
struct RuleKey {
    from_os: Platform,
    to_os: Platform,
    from_key: u16,
    context: RuleContext,
}

/// In-memory rule table with fast lookup.
pub struct KeyMapper {
    rules: RwLock<HashMap<RuleKey, MappingRule>>,
    source_path: Option<String>,
}

impl KeyMapper {
    pub fn new() -> Self {
        Self {
            rules: RwLock::new(HashMap::new()),
            source_path: None,
        }
    }

    pub fn from_yaml(path: impl AsRef<Path>) -> Result<Self> {
        let mut mapper = Self::new();
        mapper.load_yaml(path)?;
        Ok(mapper)
    }

    pub fn load_yaml(&mut self, path: impl AsRef<Path>) -> Result<()> {
        let path = path.as_ref();
        let content = std::fs::read_to_string(path)
            .map_err(|e| FlowGridError::config(format!("Failed to read keymap file: {e}")))?;
        self.load_yaml_str(&content)?;
        self.source_path = Some(path.display().to_string());
        info!("Loaded keymap from {}", path.display());
        Ok(())
    }

    pub fn load_yaml_str(&mut self, content: &str) -> Result<()> {
        #[derive(Debug, Deserialize)]
        struct KeymapFile {
            version: String,
            rules: Vec<MappingRule>,
        }

        let file: KeymapFile = serde_yaml::from_str(content)
            .map_err(|e| FlowGridError::config(format!("YAML parse error: {e}")))?;

        if file.version != "1.0" {
            warn!("Keymap version {} (expected 1.0)", file.version);
        }

        let mut rules = self.rules.write().map_err(|e| FlowGridError::config(format!("Lock poison: {e}")))?;
        rules.clear();

        for rule in file.rules {
            let from_os = platform_from_name(&rule.from_os)
                .ok_or_else(|| FlowGridError::config(format!("Unknown fromOS: {}", rule.from_os)))?;
            let to_os = platform_from_name(&rule.to_os)
                .ok_or_else(|| FlowGridError::config(format!("Unknown toOS: {}", rule.to_os)))?;
            let key = RuleKey {
                from_os,
                to_os,
                from_key: rule.from_key,
                context: rule.context.clone(),
            };
            debug!(
                "Registering rule: {} -> {} key 0x{:04X} context {}",
                rule.from_os,
                rule.to_os,
                rule.from_key,
                rule.context.as_str()
            );
            rules.insert(key, rule);
        }

        info!("Loaded {} keymap rules", rules.len());
        Ok(())
    }

    /// Lookup a rule by (from_os, to_os, key, context).
    /// Falls back to `RuleContext::Global` if no app-specific rule is found.
    pub fn lookup(
        &self,
        from_os: Platform,
        to_os: Platform,
        key: u16,
        context: &RuleContext,
    ) -> Option<MappingRule> {
        let rules = self.rules.read().ok()?;

        // Try exact context match first
        let key_exact = RuleKey {
            from_os,
            to_os,
            from_key: key,
            context: context.clone(),
        };
        if let Some(rule) = rules.get(&key_exact) {
            return Some(rule.clone());
        }

        // Fallback to global
        if !matches!(context, RuleContext::Global) {
            let key_global = RuleKey {
                from_os,
                to_os,
                from_key: key,
                context: RuleContext::Global,
            };
            if let Some(rule) = rules.get(&key_global) {
                return Some(rule.clone());
            }
        }

        None
    }

    pub fn rule_count(&self) -> usize {
        self.rules.read().map(|r| r.len()).unwrap_or(0)
    }

    pub fn source_path(&self) -> Option<&str> {
        self.source_path.as_deref()
    }

    /// Return all rules as a Vec (stable order by from_os, to_os, from_key).
    pub fn all_rules(&self) -> Vec<MappingRule> {
        let rules = self.rules.read().ok();
        rules.map(|r| {
            let mut v: Vec<_> = r.values().cloned().collect();
            v.sort_by(|a, b| {
                (&a.from_os, &a.to_os, a.from_key).cmp(&(&b.from_os, &b.to_os, b.from_key))
            });
            v
        }).unwrap_or_default()
    }

    /// Add a single rule to the table.
    pub fn add_rule(&self, rule: MappingRule) -> Result<()> {
        let from_os = platform_from_name(&rule.from_os)
            .ok_or_else(|| FlowGridError::config(format!("Unknown fromOS: {}", rule.from_os)))?;
        let to_os = platform_from_name(&rule.to_os)
            .ok_or_else(|| FlowGridError::config(format!("Unknown toOS: {}", rule.to_os)))?;
        let key = RuleKey {
            from_os,
            to_os,
            from_key: rule.from_key,
            context: rule.context.clone(),
        };
        let mut rules = self.rules.write().map_err(|e| FlowGridError::config(format!("Lock poison: {e}")))?;
        rules.insert(key, rule);
        Ok(())
    }

    /// Remove the rule at the given index (stable order by all_rules).
    pub fn remove_rule_by_index(&self, index: usize) -> Result<()> {
        let mut rules = self.rules.write().map_err(|e| FlowGridError::config(format!("Lock poison: {e}")))?;
        let mut v: Vec<_> = rules.values().cloned().collect();
        v.sort_by(|a, b| {
            (&a.from_os, &a.to_os, a.from_key).cmp(&(&b.from_os, &b.to_os, b.from_key))
        });
        if index >= v.len() {
            return Err(FlowGridError::config(format!("Rule index {index} out of range ({} rules)", v.len())));
        }
        v.remove(index);
        rules.clear();
        for rule in v {
            let from_os = match platform_from_name(&rule.from_os) {
                Some(p) => p,
                None => continue,
            };
            let to_os = match platform_from_name(&rule.to_os) {
                Some(p) => p,
                None => continue,
            };
            let key = RuleKey {
                from_os,
                to_os,
                from_key: rule.from_key,
                context: rule.context.clone(),
            };
            rules.insert(key, rule);
        }
        Ok(())
    }

    /// Replace the entire rule set atomically.
    pub fn replace_rules(&self, new_rules: Vec<MappingRule>) -> Result<()> {
        let mut rules = self.rules.write().map_err(|e| FlowGridError::config(format!("Lock poison: {e}")))?;
        rules.clear();
        for rule in new_rules {
            let from_os = match platform_from_name(&rule.from_os) {
                Some(p) => p,
                None => {
                    warn!("Skipping rule with unknown fromOS: {}", rule.from_os);
                    continue;
                }
            };
            let to_os = match platform_from_name(&rule.to_os) {
                Some(p) => p,
                None => {
                    warn!("Skipping rule with unknown toOS: {}", rule.to_os);
                    continue;
                }
            };
            let key = RuleKey {
                from_os,
                to_os,
                from_key: rule.from_key,
                context: rule.context.clone(),
            };
            rules.insert(key, rule);
        }
        info!("Replaced rule set with {} rules", rules.len());
        Ok(())
    }
}

impl Default for KeyMapper {
    fn default() -> Self {
        Self::new()
    }
}

fn platform_from_name(name: &str) -> Option<Platform> {
    match name.to_ascii_lowercase().as_str() {
        "macos" | "mac" => Some(Platform::MacOS),
        "windows" | "win" => Some(Platform::Windows),
        "linux" => Some(Platform::Linux),
        "android" => Some(Platform::Android),
        "ios" => Some(Platform::IOS),
        _ => None,
    }
}

/// Convenience: create a shared Arc mapper.
pub fn create_shared_mapper() -> Arc<KeyMapper> {
    Arc::new(KeyMapper::new())
}

#[cfg(test)]
mod tests {
    use super::*;

    const TEST_YAML: &str = r#"
version: "1.0"
rules:
  - fromOS: macos
    toOS: linux
    fromKey: 0x001E
    toKey: 0x001E
    modifiers: 0x00
    context: global
  - fromOS: windows
    toOS: linux
    fromKey: 0x0041
    toKey: 0x0061
    modifiers: 0x00
    context: global
  - fromOS: macos
    toOS: linux
    fromKey: 0x001E
    toKey: 0x0020
    modifiers: 0x00
    context: app:com.example.app
"#;

    #[test]
    fn test_load_yaml_and_lookup() {
        let mut mapper = KeyMapper::new();
        mapper.load_yaml_str(TEST_YAML).unwrap();
        assert_eq!(mapper.rule_count(), 3);

        // Exact match
        let rule = mapper.lookup(Platform::MacOS, Platform::Linux, 0x001E, &RuleContext::Global).unwrap();
        assert_eq!(rule.to_key, 0x001E);
        assert_eq!(rule.from_os, "macos");

        // Windows -> Linux remapping
        let rule = mapper.lookup(Platform::Windows, Platform::Linux, 0x0041, &RuleContext::Global).unwrap();
        assert_eq!(rule.to_key, 0x0061);

        // No match
        assert!(mapper.lookup(Platform::Linux, Platform::Linux, 0x9999, &RuleContext::Global).is_none());
    }

    #[test]
    fn test_context_fallback() {
        let mut mapper = KeyMapper::new();
        mapper.load_yaml_str(TEST_YAML).unwrap();

        // App-specific rule exists
        let app_ctx = RuleContext::App("com.example.app".to_string());
        let rule = mapper.lookup(Platform::MacOS, Platform::Linux, 0x001E, &app_ctx).unwrap();
        assert_eq!(rule.to_key, 0x0020);

        // Different app falls back to global
        let other_app = RuleContext::App("other.app".to_string());
        let rule = mapper.lookup(Platform::MacOS, Platform::Linux, 0x001E, &other_app).unwrap();
        assert_eq!(rule.to_key, 0x001E);
    }

    #[test]
    fn test_replace_rules() {
        let mut mapper = KeyMapper::new();
        mapper.load_yaml_str(TEST_YAML).unwrap();
        assert_eq!(mapper.rule_count(), 3);

        let new_rules = vec![
            MappingRule {
                from_os: "linux".to_string(),
                to_os: "macos".to_string(),
                from_key: 0x1234,
                to_key: 0x5678,
                modifiers: 0,
                context: RuleContext::Global,
            },
        ];
        mapper.replace_rules(new_rules).unwrap();
        assert_eq!(mapper.rule_count(), 1);

        let rule = mapper.lookup(Platform::Linux, Platform::MacOS, 0x1234, &RuleContext::Global).unwrap();
        assert_eq!(rule.to_key, 0x5678);
    }

    #[test]
    fn test_rule_context_parse() {
        assert_eq!(RuleContext::parse("global"), RuleContext::Global);
        assert_eq!(RuleContext::parse("game"), RuleContext::Game);
        assert_eq!(RuleContext::parse("app:foo"), RuleContext::App("foo".to_string()));
        assert_eq!(RuleContext::parse("unknown"), RuleContext::Global);
    }

    #[test]
    fn test_rule_context_as_str() {
        assert_eq!(RuleContext::Global.as_str(), "global");
        assert_eq!(RuleContext::Game.as_str(), "game");
        assert_eq!(RuleContext::App("foo".to_string()).as_str(), "app:foo");
    }

    #[test]
    fn test_invalid_yaml() {
        let mut mapper = KeyMapper::new();
        let result = mapper.load_yaml_str("invalid: yaml: [");
        assert!(result.is_err());
    }

    #[test]
    fn test_unknown_platform() {
        let mut mapper = KeyMapper::new();
        let yaml = r#"
version: "1.0"
rules:
  - fromOS: unknown_os
    toOS: linux
    fromKey: 0x0001
    toKey: 0x0001
    modifiers: 0x00
    context: global
"#;
        let result = mapper.load_yaml_str(yaml);
        assert!(result.is_err());
    }

    #[test]
    fn test_all_rules_sorted() {
        let mut mapper = KeyMapper::new();
        mapper.load_yaml_str(TEST_YAML).unwrap();
        let rules = mapper.all_rules();
        assert_eq!(rules.len(), 3);
        // Should be sorted by (from_os, to_os, from_key)
        assert_eq!(rules[0].from_os, "macos");
        assert_eq!(rules[1].from_os, "macos");
        assert_eq!(rules[2].from_os, "windows");
    }

    #[test]
    fn test_add_rule() {
        let mapper = KeyMapper::new();
        let rule = MappingRule {
            from_os: "linux".to_string(),
            to_os: "macos".to_string(),
            from_key: 0x1234,
            to_key: 0x5678,
            modifiers: 0,
            context: RuleContext::Global,
        };
        mapper.add_rule(rule).unwrap();
        assert_eq!(mapper.rule_count(), 1);
        let found = mapper.lookup(Platform::Linux, Platform::MacOS, 0x1234, &RuleContext::Global).unwrap();
        assert_eq!(found.to_key, 0x5678);
    }

    #[test]
    fn test_remove_rule_by_index() {
        let mut mapper = KeyMapper::new();
        mapper.load_yaml_str(TEST_YAML).unwrap();
        assert_eq!(mapper.rule_count(), 3);
        mapper.remove_rule_by_index(1).unwrap();
        assert_eq!(mapper.rule_count(), 2);
        // Out of range should fail
        assert!(mapper.remove_rule_by_index(10).is_err());
    }
}
