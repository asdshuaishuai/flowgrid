use serde::{Deserialize, Serialize};

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
#[derive(Default)]
pub enum PreferredTransport {
    #[serde(rename = "nearLink")]
    NearLink,
    #[serde(rename = "directLink")]
    DirectLink,
    #[serde(rename = "auto")]
    #[default]
    Auto,
}


#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct AppSettings {
    #[serde(default)]
    pub preferred_transport: PreferredTransport,
    #[serde(default = "default_true")]
    pub key_mapping_enabled: bool,
    #[serde(default = "default_true")]
    pub clipboard_sync_enabled: bool,
    #[serde(default)]
    pub mouse_smoothing_enabled: bool,
    #[serde(default = "default_true")]
    pub auto_connect_on_launch: bool,
    #[serde(default = "default_true")]
    pub dtls_enabled: bool,
}

fn default_true() -> bool {
    true
}

impl Default for AppSettings {
    fn default() -> Self {
        Self {
            preferred_transport: PreferredTransport::default(),
            key_mapping_enabled: true,
            clipboard_sync_enabled: true,
            mouse_smoothing_enabled: false,
            auto_connect_on_launch: true,
            dtls_enabled: true,
        }
    }
}
