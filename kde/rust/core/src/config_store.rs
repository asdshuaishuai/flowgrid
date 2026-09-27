use crate::app_settings::AppSettings;
use crate::error::{FlowGridError, Result};
use dirs::config_dir;
use serde_yaml;
use std::fs;
use std::path::PathBuf;
use tracing::{debug, info, warn};

const CONFIG_DIR: &str = "flowgrid";
const SETTINGS_FILE: &str = "flowgridrc";
const KEYMAP_FILE: &str = "keymap.yaml";
const DEVICES_FILE: &str = "devices.json";

pub struct ConfigStore {
    config_dir: PathBuf,
}

impl ConfigStore {
    pub fn new() -> Result<Self> {
        let dir = config_dir()
            .ok_or_else(|| FlowGridError::config("No config directory found"))?
            .join(CONFIG_DIR);
        if !dir.exists() {
            fs::create_dir_all(&dir).map_err(|e| FlowGridError::io(format!("{e}")))?;
        }
        info!("Config directory: {}", dir.display());
        Ok(Self { config_dir: dir })
    }

    /// Create a temporary in-memory config store (no persistence).
    pub fn new_in_memory() -> Self {
        let dir = std::env::temp_dir().join("flowgrid-tmp-").join(std::process::id().to_string());
        let _ = fs::create_dir_all(&dir);
        Self { config_dir: dir }
    }

    pub fn with_path(config_dir: PathBuf) -> Self {
        Self { config_dir }
    }

    pub fn settings_path(&self) -> PathBuf {
        self.config_dir.join(SETTINGS_FILE)
    }

    pub fn keymap_path(&self) -> PathBuf {
        self.config_dir.join(KEYMAP_FILE)
    }

    pub fn devices_path(&self) -> PathBuf {
        self.config_dir.join(DEVICES_FILE)
    }

    pub fn load_settings(&self) -> Result<AppSettings> {
        let path = self.settings_path();
        if !path.exists() {
            debug!("Settings file not found, using defaults");
            return Ok(AppSettings::default());
        }
        let data = fs::read_to_string(&path).map_err(|e| FlowGridError::io(format!("{e}")))?;
        let settings: AppSettings =
            serde_yaml::from_str(&data).map_err(|e| FlowGridError::config(format!("{e}")))?;
        info!("Loaded settings from {}", path.display());
        Ok(settings)
    }

    pub fn save_settings(&self, settings: &AppSettings) -> Result<()> {
        let path = self.settings_path();
        let data =
            serde_yaml::to_string(settings).map_err(|e| FlowGridError::config(format!("{e}")))?;
        fs::write(&path, data).map_err(|e| FlowGridError::io(format!("{e}")))?;
        info!("Saved settings to {}", path.display());
        Ok(())
    }

    pub fn ensure_keymap_exists(&self) -> Result<()> {
        let path = self.keymap_path();
        if !path.exists() {
            let default = "version: \"1.0\"\nrules: []\n";
            fs::write(&path, default).map_err(|e| FlowGridError::io(format!("{e}")))?;
            info!("Created default keymap at {}", path.display());
        }
        Ok(())
    }

    pub fn load_keymap_yaml(&self) -> Result<String> {
        let path = self.keymap_path();
        if !path.exists() {
            self.ensure_keymap_exists()?;
        }
        fs::read_to_string(&path).map_err(|e| FlowGridError::io(format!("{e}")))
    }

    pub fn watch_keymap(&self) -> Result<notify::RecommendedWatcher> {
        let _path = self.keymap_path();
        let watcher = notify::recommended_watcher(|res: std::result::Result<notify::Event, notify::Error>| {
            match res {
                Ok(event) => {
                    info!("Keymap file changed: {:?}", event.kind);
                }
                Err(e) => {
                    warn!("Keymap watch error: {}", e);
                }
            }
        })
        .map_err(|e| FlowGridError::io(format!("{e}")))?;
        Ok(watcher)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::io::Write;
    use tempfile::NamedTempFile;

    #[test]
    fn test_load_save_settings() {
        let mut tmp = NamedTempFile::new().unwrap();
        let s = AppSettings::default();
        let yaml = serde_yaml::to_string(&s).unwrap();
        tmp.write_all(yaml.as_bytes()).unwrap();

        let store = ConfigStore {
            config_dir: tmp.path().parent().unwrap().to_path_buf(),
        };
        let loaded = store.load_settings().unwrap();
        assert_eq!(loaded.key_mapping_enabled, s.key_mapping_enabled);
    }
}
