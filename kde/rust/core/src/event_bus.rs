//! Event bus for reactive UI updates.
//!
//! Uses a tokio broadcast channel so multiple subscribers (e.g. UI bridge,
//! logging, auto-connect) can observe state changes without polling.

use crate::device::Platform;
use tokio::sync::broadcast;

/// Capacity of the broadcast channel.
const EVENT_BUS_CAPACITY: usize = 256;

/// All events that can be emitted by the core system.
#[derive(Debug, Clone)]
pub enum FlowGridEvent {
    /// A new device was discovered via BLE or mDNS.
    DeviceDiscovered {
        id: String,
        name: String,
        platform: Platform,
        transport_type: String,
        rssi: Option<i32>,
    },
    /// Device connection state changed.
    DeviceStateChanged {
        id: String,
        state: DeviceConnectionState,
    },
    /// Device was removed from the list.
    DeviceRemoved { id: String },
    /// Latency measurement updated.
    LatencyUpdated { id: String, latency_ms: f64, jitter_ms: f64 },
    /// Key map rules changed.
    KeyMapChanged,
    /// Application settings changed.
    SettingChanged { key: String, value: bool },
    /// A generic error occurred.
    Error { message: String },
    /// Scanning state changed.
    ScanStateChanged { scanning: bool },
}

/// Serializable connection state for UI consumption.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum DeviceConnectionState {
    Idle,
    Discovered,
    Connecting,
    Connected,
    Reconnecting,
    Disconnecting,
    Error,
}

impl std::fmt::Display for DeviceConnectionState {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            DeviceConnectionState::Idle => write!(f, "idle"),
            DeviceConnectionState::Discovered => write!(f, "discovered"),
            DeviceConnectionState::Connecting => write!(f, "connecting"),
            DeviceConnectionState::Connected => write!(f, "connected"),
            DeviceConnectionState::Reconnecting => write!(f, "reconnecting"),
            DeviceConnectionState::Disconnecting => write!(f, "disconnecting"),
            DeviceConnectionState::Error => write!(f, "error"),
        }
    }
}

/// Shared event bus handle.
#[derive(Clone)]
pub struct EventBus {
    tx: broadcast::Sender<FlowGridEvent>,
}

impl EventBus {
    pub fn new() -> Self {
        let (tx, _rx) = broadcast::channel(EVENT_BUS_CAPACITY);
        Self { tx }
    }

    pub fn subscribe(&self) -> broadcast::Receiver<FlowGridEvent> {
        self.tx.subscribe()
    }

    pub fn emit(&self, event: FlowGridEvent) {
        let _ = self.tx.send(event); // ignore "no receivers" error
    }
}

impl Default for EventBus {
    fn default() -> Self {
        Self::new()
    }
}
