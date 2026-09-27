use serde::{Deserialize, Serialize};
use std::net::SocketAddr;

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize, Hash)]
#[repr(u8)]
#[derive(Default)]
pub enum Platform {
    MacOS = 0x01,
    Windows = 0x02,
    #[default]
    Linux = 0x03,
    Android = 0x04,
    IOS = 0x05,
}

impl Platform {
    pub fn as_u8(self) -> u8 {
        self as u8
    }

    pub fn from_u8(v: u8) -> Option<Self> {
        match v {
            0x01 => Some(Self::MacOS),
            0x02 => Some(Self::Windows),
            0x03 => Some(Self::Linux),
            0x04 => Some(Self::Android),
            0x05 => Some(Self::IOS),
            _ => None,
        }
    }

    pub fn name(self) -> &'static str {
        match self {
            Platform::MacOS => "macOS",
            Platform::Windows => "Windows",
            Platform::Linux => "Linux",
            Platform::Android => "Android",
            Platform::IOS => "iOS",
        }
    }
}


#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[repr(u8)]
#[derive(Default)]
pub enum Role {
    Host = 0x01,
    Target = 0x02,
    #[default]
    Both = 0x03,
}

impl Role {
    pub fn as_u8(self) -> u8 {
        self as u8
    }
}


#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
pub struct CapabilityFlags(pub u8);

impl CapabilityFlags {
    pub const NEAR_LINK: u8 = 0x01;
    pub const DIRECT_LINK: u8 = 0x02;
    pub const WIFI_DIRECT: u8 = 0x04;
    pub const DTLS_13: u8 = 0x08;
    pub const CLIPBOARD: u8 = 0x10;
    pub const FILE_TRANSFER: u8 = 0x20;

    pub fn has(self, cap: u8) -> bool {
        self.0 & cap != 0
    }

    pub fn set(&mut self, cap: u8) {
        self.0 |= cap;
    }
}

impl Default for CapabilityFlags {
    fn default() -> Self {
        let mut f = Self(0);
        f.set(Self::NEAR_LINK);
        f.set(Self::DIRECT_LINK);
        f.set(Self::DTLS_13);
        f.set(Self::CLIPBOARD);
        f
    }
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct Device {
    pub id: String,
    pub name: String,
    pub platform: Platform,
    pub role: Role,
    pub caps: CapabilityFlags,
    pub address: Option<SocketAddr>,
    pub latency_ms: f64,
    pub connected: bool,
    pub transport_type: String,
}

impl Device {
    pub fn new(id: impl Into<String>, name: impl Into<String>, platform: Platform) -> Self {
        Self {
            id: id.into(),
            name: name.into(),
            platform,
            role: Role::default(),
            caps: CapabilityFlags::default(),
            address: None,
            latency_ms: 0.0,
            connected: false,
            transport_type: "DirectLink".to_string(),
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ConnectionState {
    Idle,
    Scanning,
    Connecting,
    Connected,
    Reconnecting { attempt: i32 },
    Disconnecting,
}

impl ConnectionState {
    pub fn name(self) -> &'static str {
        match self {
            ConnectionState::Idle => "idle",
            ConnectionState::Scanning => "scanning",
            ConnectionState::Connecting => "connecting",
            ConnectionState::Connected => "connected",
            ConnectionState::Reconnecting { .. } => "reconnecting",
            ConnectionState::Disconnecting => "disconnecting",
        }
    }
}
