//! D-Bus service using zbus 5, exporting `org.flowgrid.Core` interface.

use crate::error::{FlowGridError, Result};
use std::sync::Arc;
use tokio::sync::RwLock;
use tokio::time::{sleep, Duration};
use tracing::{info};
use zbus::{interface, object_server::SignalEmitter, Connection};

/// D-Bus interface implementation for FlowGrid Core.
pub struct FlowGridDBusInterface {
    state: Arc<RwLock<DBusState>>,
}

#[derive(Debug, Default)]
pub struct DBusState {
    pub device_ids: Vec<String>,
    pub device_names: Vec<String>,
    pub connected: Vec<bool>,
}

impl Default for FlowGridDBusInterface {
    fn default() -> Self {
        Self::new()
    }
}

impl FlowGridDBusInterface {
    pub fn new() -> Self {
        Self {
            state: Arc::new(RwLock::new(DBusState::default())),
        }
    }

    pub fn with_state(state: Arc<RwLock<DBusState>>) -> Self {
        Self { state }
    }

    pub fn state(&self) -> Arc<RwLock<DBusState>> {
        Arc::clone(&self.state)
    }
}

#[interface(name = "org.flowgrid.Core")]
impl FlowGridDBusInterface {
    /// Connect to a remote device.
    async fn connect(&self, device_id: &str, address: &str, transport: &str) -> zbus::fdo::Result<String> {
        info!("DBus Connect requested: {device_id} at {address} via {transport}");
        Ok(format!("connect ack: {device_id}"))
    }

    /// Disconnect from a remote device.
    async fn disconnect(&self, device_id: &str) -> zbus::fdo::Result<String> {
        info!("DBus Disconnect requested: {device_id}");
        Ok(format!("disconnect ack: {device_id}"))
    }

    /// List currently known device IDs.
    async fn list_devices(&self) -> zbus::fdo::Result<Vec<String>> {
        let state = self.state.read().await;
        Ok(state.device_ids.clone())
    }

    /// Get the connection state of a specific device.
    async fn get_connection_state(&self, device_id: &str) -> zbus::fdo::Result<String> {
        let state = self.state.read().await;
        if let Some(idx) = state.device_ids.iter().position(|id| id == device_id) {
            Ok(state.connected.get(idx).copied().unwrap_or(false).to_string())
        } else {
            Err(zbus::fdo::Error::UnknownObject(format!("Device {device_id} not found")))
        }
    }

    // -----------------------------------------------------------------------
    // Signals
    // -----------------------------------------------------------------------

    #[zbus(signal)]
    async fn device_connected(
        emitter: &SignalEmitter<'_>,
        device_id: &str,
        name: &str,
    ) -> zbus::Result<()>;

    #[zbus(signal)]
    async fn device_disconnected(
        emitter: &SignalEmitter<'_>,
        device_id: &str,
        reason: &str,
    ) -> zbus::Result<()>;

    #[zbus(signal)]
    async fn latency_update(
        emitter: &SignalEmitter<'_>,
        device_id: &str,
        latency_ms: f64,
        quality: &str,
    ) -> zbus::Result<()>;
}

/// D-Bus service runner.
pub struct DBusService {
    connection: Connection,
}

impl DBusService {
    pub async fn new() -> Result<Self> {
        let connection = Connection::session()
            .await
            .map_err(|e| FlowGridError::io(format!("Failed to connect to D-Bus session bus: {e}")))?;

        let interface = FlowGridDBusInterface::new();
        connection
            .object_server()
            .at("/org/flowgrid/Core", interface)
            .await
            .map_err(|e| FlowGridError::io(format!("Failed to register D-Bus object: {e}")))?;

        connection
            .request_name("org.flowgrid")
            .await
            .map_err(|e| FlowGridError::io(format!("Failed to request D-Bus name: {e}")))?;

        info!("D-Bus service registered at org.flowgrid /org/flowgrid/Core");
        Ok(Self { connection })
    }

    pub fn connection(&self) -> &Connection {
        &self.connection
    }

    /// Emit the DeviceConnected signal.
    pub async fn emit_device_connected(&self, device_id: &str, name: &str) -> Result<()> {
        let emitter = SignalEmitter::new(&self.connection, "/org/flowgrid/Core")
            .map_err(|e| FlowGridError::io(format!("SignalEmitter creation failed: {e}")))?;
        FlowGridDBusInterface::device_connected(&emitter, device_id, name)
            .await
            .map_err(|e| FlowGridError::io(format!("D-Bus signal emit failed: {e}")))?;
        Ok(())
    }

    /// Emit the DeviceDisconnected signal.
    pub async fn emit_device_disconnected(&self, device_id: &str, reason: &str) -> Result<()> {
        let emitter = SignalEmitter::new(&self.connection, "/org/flowgrid/Core")
            .map_err(|e| FlowGridError::io(format!("SignalEmitter creation failed: {e}")))?;
        FlowGridDBusInterface::device_disconnected(&emitter, device_id, reason)
            .await
            .map_err(|e| FlowGridError::io(format!("D-Bus signal emit failed: {e}")))?;
        Ok(())
    }

    /// Emit the LatencyUpdate signal.
    pub async fn emit_latency_update(&self, device_id: &str, latency_ms: f64, quality: &str) -> Result<()> {
        let emitter = SignalEmitter::new(&self.connection, "/org/flowgrid/Core")
            .map_err(|e| FlowGridError::io(format!("SignalEmitter creation failed: {e}")))?;
        FlowGridDBusInterface::latency_update(&emitter, device_id, latency_ms, quality)
            .await
            .map_err(|e| FlowGridError::io(format!("D-Bus signal emit failed: {e}")))?;
        Ok(())
    }

    /// Keep the service alive (blocks until the connection drops).
    pub async fn run(&self) {
        loop {
            sleep(Duration::from_secs(60)).await;
        }
    }
}

impl Clone for FlowGridDBusInterface {
    fn clone(&self) -> Self {
        Self {
            state: Arc::clone(&self.state),
        }
    }
}
