//! Device manager with tokio mpsc actor pattern, connection state machine,
//! heartbeat, reconnect, and frame routing.

use crate::device::{ConnectionState, Device, Platform};
use crate::error::{FlowGridError, Result};
use crate::event_bus::{DeviceConnectionState, EventBus, FlowGridEvent};
use crate::hal::PlatformHal;
use crate::keymapper::{KeyMapper, RuleContext};
use crate::protocol_ffi::{
    HeartbeatTracker, LatencyMonitor, ReconnectState, SequenceTracker,
};
use crate::transport::{Transport, TransportType};
use crate::transport::direct_link::DirectLinkTransport;
use crate::transport::near_link::NearLinkTransport;
use std::collections::HashMap;
use std::sync::atomic::{AtomicU32, Ordering};
use std::net::SocketAddr;
use std::sync::Arc;
use tokio::sync::{mpsc, oneshot, RwLock};
use tokio::time::{interval, Duration, Instant};
use tracing::{debug, error, info, trace, warn};

static SEQ_INIT_COUNTER: AtomicU32 = AtomicU32::new(1);

// ---------------------------------------------------------------------------
// Public messages to the DeviceManager actor
// ---------------------------------------------------------------------------

pub enum DeviceManagerCommand {
    Connect {
        device_id: String,
        addr: SocketAddr,
        transport_type: TransportType,
        respond_to: oneshot::Sender<Result<()>>,
    },
    Disconnect {
        device_id: String,
        respond_to: oneshot::Sender<Result<()>>,
    },
    Scan {
        transport_type: TransportType,
        respond_to: oneshot::Sender<Result<()>>,
    },
    ListDevices {
        respond_to: oneshot::Sender<Vec<Device>>,
    },
    GetDeviceState {
        device_id: String,
        respond_to: oneshot::Sender<Option<ConnectionState>>,
    },
    SendFrame {
        device_id: String,
        frame: Vec<u8>,
        respond_to: oneshot::Sender<Result<()>>,
    },
    IncomingFrame {
        device_id: String,
        frame: Vec<u8>,
    },
    Shutdown,
}

// ---------------------------------------------------------------------------
// Per-device connection context
// ---------------------------------------------------------------------------

struct DeviceConnection {
    device: Device,
    state: ConnectionState,
    transport: Box<dyn Transport>,
    #[allow(dead_code)]
    transport_type: TransportType,
    sequence: SequenceTracker,
    heartbeat: HeartbeatTracker,
    reconnect: ReconnectState,
    latency: LatencyMonitor,
    last_heartbeat: Instant,
    reconnect_attempt: i32,
    last_ping_time: Option<Instant>,
    reconnect_deadline: Option<Instant>,
}

impl DeviceConnection {
    fn new(
        device: Device,
        transport_type: TransportType,
        seq_init: u32,
    ) -> Self {
        let transport: Box<dyn Transport> = match transport_type {
            TransportType::NearLink => Box::new(NearLinkTransport::new()),
            TransportType::DirectLink => Box::new(DirectLinkTransport::new()),
        };
        Self {
            device,
            state: ConnectionState::Idle,
            transport,
            transport_type,
            sequence: SequenceTracker::new(seq_init),
            heartbeat: HeartbeatTracker::new(),
            reconnect: ReconnectState::new(),
            latency: LatencyMonitor::new(100),
            last_heartbeat: Instant::now(),
            reconnect_attempt: 0,
            last_ping_time: None,
            reconnect_deadline: None,
        }
    }
}

// ---------------------------------------------------------------------------
// DeviceManager actor
// ---------------------------------------------------------------------------

pub struct DeviceManager {
    rx: mpsc::Receiver<DeviceManagerCommand>,
    devices: HashMap<String, DeviceConnection>,
    #[allow(dead_code)]
    known_devices: HashMap<String, Device>,
    heartbeat_interval: Duration,
    heartbeat_timeout: Duration,
    max_reconnect_attempts: i32,
    event_bus: EventBus,
    hal: Option<Box<dyn PlatformHal>>,
    keymapper: Option<Arc<KeyMapper>>,
}

impl DeviceManager {
    pub fn new(
        rx: mpsc::Receiver<DeviceManagerCommand>,
        event_bus: EventBus,
        hal: Option<Box<dyn PlatformHal>>,
        keymapper: Option<Arc<KeyMapper>>,
    ) -> Self {
        Self {
            rx,
            devices: HashMap::new(),
            known_devices: HashMap::new(),
            heartbeat_interval: Duration::from_secs(2),
            heartbeat_timeout: Duration::from_secs(6),
            max_reconnect_attempts: 5,
            event_bus,
            hal,
            keymapper,
        }
    }

    pub fn spawn(
        rx: mpsc::Receiver<DeviceManagerCommand>,
        event_bus: EventBus,
        hal: Option<Box<dyn PlatformHal>>,
        keymapper: Option<Arc<KeyMapper>>,
    ) -> Arc<RwLock<DeviceManagerHandle>> {
        let manager = Self::new(rx, event_bus, hal, keymapper);
        let handle = Arc::new(RwLock::new(DeviceManagerHandle::new()));
        let handle_clone = Arc::clone(&handle);
        tokio::spawn(async move {
            manager.run(handle_clone).await;
        });
        handle
    }

    async fn run(mut self, handle: Arc<RwLock<DeviceManagerHandle>>) {
        let mut tick = interval(self.heartbeat_interval);
        let mut recv_tick = interval(Duration::from_millis(1));
        loop {
            tokio::select! {
                biased;
                _ = recv_tick.tick() => {
                    if let Err(e) = self.recv_frames().await {
                        error!("Receive frame error: {e}");
                    }
                }
                _ = tick.tick() => {
                    if let Err(e) = self.on_tick().await {
                        error!("Heartbeat tick error: {e}");
                    }
                }
                Some(cmd) = self.rx.recv() => {
                    match cmd {
                        DeviceManagerCommand::Shutdown => {
                            info!("DeviceManager shutting down");
                            break;
                        }
                        other => {
                            if let Err(e) = self.handle_command(other, &handle).await {
                                error!("Command error: {e}");
                            }
                        }
                    }
                }
            }
        }

        // Clean disconnect all
        for (id, mut conn) in self.devices.drain() {
            info!("Disconnecting {id} on shutdown");
            let _ = conn.transport.disconnect();
        }
    }

    async fn handle_command(
        &mut self,
        cmd: DeviceManagerCommand,
        handle: &Arc<RwLock<DeviceManagerHandle>>,
    ) -> Result<()> {
        match cmd {
            DeviceManagerCommand::Connect {
                device_id,
                addr,
                transport_type,
                respond_to,
            } => {
                let result = self.connect_device(&device_id, addr, transport_type).await;
                if result.is_ok() {
                    let mut h = handle.write().await;
                    h.update_state(&device_id, ConnectionState::Connected);
                }
                let _ = respond_to.send(result);
            }
            DeviceManagerCommand::Disconnect { device_id, respond_to } => {
                let result = self.disconnect_device(&device_id).await;
                let mut h = handle.write().await;
                h.update_state(&device_id, ConnectionState::Idle);
                let _ = respond_to.send(result);
            }
            DeviceManagerCommand::ListDevices { respond_to } => {
                let list: Vec<Device> = self.devices.values().map(|c| c.device.clone()).collect();
                let _ = respond_to.send(list);
            }
            DeviceManagerCommand::GetDeviceState { device_id, respond_to } => {
                let state = self.devices.get(&device_id).map(|c| c.state);
                let _ = respond_to.send(state);
            }
            DeviceManagerCommand::SendFrame { device_id, frame, respond_to } => {
                let result = self.send_frame(&device_id, &frame).await;
                let _ = respond_to.send(result);
            }
            DeviceManagerCommand::Scan { transport_type, respond_to } => {
                let result = self.scan_devices(transport_type).await;
                let _ = respond_to.send(result);
            }
            DeviceManagerCommand::IncomingFrame { device_id, frame } => {
                if let Err(e) = self.route_incoming_frame(&device_id, &frame).await {
                    warn!("Incoming frame routing error for {device_id}: {e}");
                }
            }
            DeviceManagerCommand::Shutdown => {}
        }
        Ok(())
    }

    async fn connect_device(
        &mut self,
        device_id: &str,
        addr: SocketAddr,
        transport_type: TransportType,
    ) -> Result<()> {
        if self.devices.contains_key(device_id) {
            return Err(FlowGridError::AlreadyConnected);
        }

        let device = Device::new(device_id, device_id, Platform::Linux);
        let seq_init = SEQ_INIT_COUNTER.fetch_add(1, Ordering::Relaxed);
        let mut conn = DeviceConnection::new(device, transport_type, seq_init);
        conn.state = ConnectionState::Connecting;
        conn.device.connected = true;
        conn.device.transport_type = transport_type.name().to_string();
        conn.device.address = Some(addr);

        self.event_bus.emit(FlowGridEvent::DeviceStateChanged {
            id: device_id.to_string(),
            state: DeviceConnectionState::Connecting,
        });

        info!("Connecting to {device_id} at {addr} via {}", transport_type.name());
        conn.transport.connect(addr)?;

        conn.state = ConnectionState::Connected;
        conn.last_heartbeat = Instant::now();
        conn.reconnect.set_connected();
        conn.reconnect_attempt = 0;

        self.devices.insert(device_id.to_string(), conn);
        self.event_bus.emit(FlowGridEvent::DeviceStateChanged {
            id: device_id.to_string(),
            state: DeviceConnectionState::Connected,
        });
        info!("Device {device_id} connected");
        Ok(())
    }

    async fn disconnect_device(&mut self, device_id: &str) -> Result<()> {
        let mut conn = self.devices.remove(device_id)
            .ok_or_else(|| FlowGridError::DeviceNotFound(device_id.to_string()))?;
        conn.state = ConnectionState::Disconnecting;
        self.event_bus.emit(FlowGridEvent::DeviceStateChanged {
            id: device_id.to_string(),
            state: DeviceConnectionState::Disconnecting,
        });
        conn.transport.disconnect()?;
        conn.device.connected = false;
        self.event_bus.emit(FlowGridEvent::DeviceStateChanged {
            id: device_id.to_string(),
            state: DeviceConnectionState::Idle,
        });
        self.event_bus.emit(FlowGridEvent::DeviceRemoved {
            id: device_id.to_string(),
        });
        info!("Device {device_id} disconnected");
        Ok(())
    }

    async fn send_frame(&mut self, device_id: &str, frame: &[u8]) -> Result<()> {
        let conn = self.devices.get_mut(device_id)
            .ok_or(FlowGridError::NotConnected)?;
        if !conn.transport.is_connected() {
            return Err(FlowGridError::ConnectionLost);
        }
        conn.transport.send_frame(frame)?;
        trace!("Sent {} bytes to {device_id}", frame.len());
        Ok(())
    }

    async fn recv_frames(&mut self) -> Result<()> {
        let mut received = Vec::new();
        for (id, conn) in self.devices.iter_mut() {
            if conn.state != ConnectionState::Connected {
                continue;
            }
            let mut buf = [0u8; 256];
            match conn.transport.recv_frame(&mut buf) {
                Ok(0) => {}
                Ok(n) => {
                    received.push((id.clone(), buf[..n].to_vec()));
                }
                Err(e) => {
                    warn!("Receive frame error from {id}: {e}");
                }
            }
        }
        for (id, frame) in received {
            if let Err(e) = self.route_incoming_frame(&id, &frame).await {
                warn!("Incoming frame routing error for {id}: {e}");
            }
        }
        Ok(())
    }

    async fn scan_devices(&mut self, transport_type: TransportType) -> Result<()> {
        self.event_bus.emit(FlowGridEvent::ScanStateChanged { scanning: true });
        match transport_type {
            TransportType::NearLink => {
                let _transport = NearLinkTransport::new();
                match NearLinkTransport::scan_ble().await {
                    Ok(devices) => {
                        for d in devices {
                            self.event_bus.emit(FlowGridEvent::DeviceDiscovered {
                                id: format!("{}-{:02x}", d.name, d.platform),
                                name: d.name.clone(),
                                platform: match d.platform {
                                    0x01 => Platform::MacOS,
                                    0x02 => Platform::Windows,
                                    0x03 => Platform::Linux,
                                    _ => Platform::Linux,
                                },
                                transport_type: "NearLink".to_string(),
                                rssi: Some(d.rssi),
                            });
                        }
                    }
                    Err(e) => {
                        self.event_bus.emit(FlowGridEvent::Error {
                            message: format!("BLE scan failed: {e}"),
                        });
                    }
                }
            }
            TransportType::DirectLink => {
                // mDNS scan is handled by DirectLinkTransport internals
                self.event_bus.emit(FlowGridEvent::Error {
                    message: "DirectLink scan not yet implemented".to_string(),
                });
            }
        }
        self.event_bus.emit(FlowGridEvent::ScanStateChanged { scanning: false });
        Ok(())
    }

    async fn route_incoming_frame(&mut self, device_id: &str, frame: &[u8]) -> Result<()> {
        let hmac_key = self.devices.get(device_id).and_then(|conn| conn.transport.hmac_key());
        let conn = self.devices.get_mut(device_id)
            .ok_or_else(|| FlowGridError::DeviceNotFound(device_id.to_string()))?;

        let decoded = if let Some(ref key) = hmac_key {
            crate::protocol_ffi::decode_frame_with_hmac(frame, key)?
        } else {
            crate::protocol_ffi::decode_frame(frame)?
        };
        let (frame_type, seq, ts, payload) = decoded;

        // Sequence check
        if let Err(e) = conn.sequence.check(seq) {
            warn!("Sequence gap from {device_id}: {e}");
        }

        match frame_type {
            0x08 => {
                // HEARTBEAT
                conn.heartbeat.received();
                conn.last_heartbeat = Instant::now();
                debug!("Heartbeat received from {device_id}");
            }
            0x09 => {
                // LATENCY_PING
                let pong = crate::protocol_ffi::payload_latency_pong(ts)?;
                let hmac_key = conn.transport.hmac_key();
                let reply = if let Some(ref key) = hmac_key {
                    crate::protocol_ffi::encode_frame_with_hmac(0x0A, conn.sequence.next(), ts, &pong, key)?
                } else {
                    crate::protocol_ffi::encode_frame(0x0A, conn.sequence.next(), ts, &pong)?
                };
                let _ = conn.transport.send_frame(&reply);
            }
            0x0A => {
                // LATENCY_PONG
                let rtt_us = if let Some(ping_time) = conn.last_ping_time {
                    ping_time.elapsed().as_micros() as u64
                } else {
                    // Fallback to protocol timestamp if no local ping time recorded
                    (std::time::SystemTime::now()
                        .duration_since(std::time::UNIX_EPOCH)
                        .unwrap_or_default()
                        .as_micros() as u64)
                        .saturating_sub(ts)
                };
                let (ms, jitter) = conn.latency.record_pong(ts, rtt_us)?;
                conn.device.latency_ms = ms;
                self.event_bus.emit(FlowGridEvent::LatencyUpdated {
                    id: device_id.to_string(),
                    latency_ms: ms,
                    jitter_ms: jitter,
                });
                debug!("Latency {device_id}: {ms:.2} ms, jitter {jitter:.2} ms");
            }
            0x01..=0x05 => {
                // HID events: inject through HAL / KeyMapper
                if let Some(ref hal) = self.hal {
                    let from_platform = conn.device.platform;
                    let km = self.keymapper.as_deref();
                    if let Err(e) = inject_hid(hal.as_ref(), km, frame_type, &payload, from_platform) {
                        warn!("HID injection failed for {device_id}: {e}");
                    } else {
                        trace!("HID frame 0x{frame_type:02X} injected from {device_id}");
                    }
                }
            }
            0x07 => {
                // CLIPBOARD: forward text content to the local X11 selection
                match crate::protocol_ffi::unmarshal_clipboard(&payload) {
                    Ok(content) if content.mime.starts_with("text/") => {
                        let text = String::from_utf8_lossy(&content.data).into_owned();
                        crate::clipboard::handle_incoming_text(&text);
                    }
                    Ok(content) => {
                        debug!("clipboard frame with unsupported mime {} ignored", content.mime);
                    }
                    Err(e) => warn!("clipboard frame from {device_id} malformed: {e}"),
                }
            }
            0x20 => {
                // DISCONNECT
                let reason = crate::protocol_ffi::unmarshal_disconnect(&payload).unwrap_or(0xFF);
                warn!("Peer {device_id} disconnected: reason 0x{reason:02X}");
                let _ = self.disconnect_device(device_id).await;
            }
            0xFF => {
                // ERROR
                warn!("Error frame from {device_id}");
            }
            _ => {
                trace!("Unknown frame type 0x{frame_type:02X} from {device_id}");
            }
        }

        Ok(())
    }

    async fn on_tick(&mut self) -> Result<()> {
        let now = Instant::now();
        let mut to_reconnect: Vec<String> = Vec::new();
        let mut to_disconnect: Vec<String> = Vec::new();

        for (id, conn) in self.devices.iter_mut() {
            match conn.state {
                ConnectionState::Connected => {
                    // Check heartbeat timeout
                    if now.duration_since(conn.last_heartbeat) > self.heartbeat_timeout {
                        warn!("Heartbeat timeout for {id}, entering reconnect");
                        let attempt = conn.reconnect_attempt + 1;
                        conn.state = ConnectionState::Reconnecting { attempt };
                        if let Some(backoff_ms) = conn.reconnect.start() {
                            conn.reconnect_deadline = Some(now + Duration::from_millis(backoff_ms));
                        }
                        self.event_bus.emit(FlowGridEvent::DeviceStateChanged {
                            id: id.clone(),
                            state: DeviceConnectionState::Reconnecting,
                        });
                        to_reconnect.push(id.clone());
                        continue;
                    }

                    // Send heartbeat
                    let hmac_key = conn.transport.hmac_key();
                    let hb = if let Some(ref key) = hmac_key {
                        crate::protocol_ffi::encode_frame_with_hmac(0x08, conn.sequence.next(), 0, &[], key)?
                    } else {
                        crate::protocol_ffi::encode_frame(0x08, conn.sequence.next(), 0, &[])?
                    };
                    if let Err(e) = conn.transport.send_frame(&hb) {
                        warn!("Failed to send heartbeat to {id}: {e}");
                        let attempt = conn.reconnect_attempt + 1;
                        conn.state = ConnectionState::Reconnecting { attempt };
                        if let Some(backoff_ms) = conn.reconnect.start() {
                            conn.reconnect_deadline = Some(now + Duration::from_millis(backoff_ms));
                        }
                        self.event_bus.emit(FlowGridEvent::DeviceStateChanged {
                            id: id.clone(),
                            state: DeviceConnectionState::Reconnecting,
                        });
                        to_reconnect.push(id.clone());
                    } else {
                        trace!("Heartbeat sent to {id}");
                    }

                    // Send latency ping
                    let ts = std::time::SystemTime::now()
                        .duration_since(std::time::UNIX_EPOCH)
                        .unwrap_or_default()
                        .as_micros() as u64;
                    let ping_payload = crate::protocol_ffi::payload_latency_ping(ts)?;
                    let hmac_key = conn.transport.hmac_key();
                    let ping = if let Some(ref key) = hmac_key {
                        crate::protocol_ffi::encode_frame_with_hmac(0x09, conn.sequence.next(), ts, &ping_payload, key)?
                    } else {
                        crate::protocol_ffi::encode_frame(0x09, conn.sequence.next(), ts, &ping_payload)?
                    };
                    conn.last_ping_time = Some(Instant::now());
                    conn.latency.record_ping(ts);
                    if let Err(e) = conn.transport.send_frame(&ping) {
                        warn!("Failed to send latency ping to {id}: {e}");
                    }
                }
                ConnectionState::Reconnecting { attempt } => {
                    if attempt > self.max_reconnect_attempts {
                        warn!("Max reconnect attempts reached for {id}, disconnecting");
                        to_disconnect.push(id.clone());
                    } else {
                        conn.reconnect_attempt = attempt;
                        // Non-blocking: only attempt reconnect if backoff deadline has passed
                        if let Some(addr) = conn.device.address
                            && conn.reconnect_deadline.is_none_or(|d| now >= d)
                        {
                            if conn.transport.connect(addr).is_ok() {
                                info!("Reconnected to {id}");
                                conn.state = ConnectionState::Connected;
                                conn.last_heartbeat = Instant::now();
                                conn.reconnect.set_connected();
                                conn.reconnect_attempt = 0;
                                conn.reconnect_deadline = None;
                                let seq_init = SEQ_INIT_COUNTER.fetch_add(1, Ordering::Relaxed);
                                conn.sequence.reset(seq_init);
                                self.event_bus.emit(FlowGridEvent::DeviceStateChanged {
                                    id: id.clone(),
                                    state: DeviceConnectionState::Connected,
                                });
                            } else {
                                conn.state = ConnectionState::Reconnecting { attempt: attempt + 1 };
                                if let Some(backoff_ms) = conn.reconnect.start() {
                                    conn.reconnect_deadline = Some(now + Duration::from_millis(backoff_ms));
                                }
                            }
                        }
                    }
                }
                _ => {}
            }
        }

        for id in to_disconnect {
            let _ = self.disconnect_device(&id).await;
        }

        Ok(())
    }
}

// ---------------------------------------------------------------------------
// Handle for external observers (state snapshots)
// ---------------------------------------------------------------------------

pub struct DeviceManagerHandle {
    states: HashMap<String, ConnectionState>,
}

impl DeviceManagerHandle {
    fn new() -> Self {
        Self {
            states: HashMap::new(),
        }
    }

    pub fn update_state(&mut self, device_id: &str, state: ConnectionState) {
        self.states.insert(device_id.to_string(), state);
    }

    pub fn get_state(&self, device_id: &str) -> Option<ConnectionState> {
        self.states.get(device_id).copied()
    }
}

// ---------------------------------------------------------------------------
// Client API (convenience wrappers around the mpsc channel)
// ---------------------------------------------------------------------------

#[derive(Clone)]
pub struct DeviceManagerClient {
    tx: mpsc::Sender<DeviceManagerCommand>,
}

impl DeviceManagerClient {
    pub fn new(tx: mpsc::Sender<DeviceManagerCommand>) -> Self {
        Self { tx }
    }

    pub async fn connect(&self, device_id: String, addr: SocketAddr, transport_type: TransportType) -> Result<()> {
        let (tx, rx) = oneshot::channel();
        self.tx.send(DeviceManagerCommand::Connect { device_id, addr, transport_type, respond_to: tx }).await
            .map_err(|e| FlowGridError::transport(format!("mpsc send failed: {e}")))?;
        rx.await.map_err(|e| FlowGridError::transport(format!("oneshot recv failed: {e}")))?
    }

    pub async fn disconnect(&self, device_id: String) -> Result<()> {
        let (tx, rx) = oneshot::channel();
        self.tx.send(DeviceManagerCommand::Disconnect { device_id, respond_to: tx }).await
            .map_err(|e| FlowGridError::transport(format!("mpsc send failed: {e}")))?;
        rx.await.map_err(|e| FlowGridError::transport(format!("oneshot recv failed: {e}")))?
    }

    pub async fn list_devices(&self) -> Vec<Device> {
        let (tx, rx) = oneshot::channel();
        let _ = self.tx.send(DeviceManagerCommand::ListDevices { respond_to: tx }).await;
        rx.await.unwrap_or_default()
    }

    pub async fn get_device_state(&self, device_id: String) -> Option<ConnectionState> {
        let (tx, rx) = oneshot::channel();
        let _ = self.tx.send(DeviceManagerCommand::GetDeviceState { device_id, respond_to: tx }).await;
        rx.await.ok().flatten()
    }

    pub async fn send_frame(&self, device_id: String, frame: Vec<u8>) -> Result<()> {
        let (tx, rx) = oneshot::channel();
        self.tx.send(DeviceManagerCommand::SendFrame { device_id, frame, respond_to: tx }).await
            .map_err(|e| FlowGridError::transport(format!("mpsc send failed: {e}")))?;
        rx.await.map_err(|e| FlowGridError::transport(format!("oneshot recv failed: {e}")))?
    }

    pub async fn scan(&self, transport_type: TransportType) -> Result<()> {
        let (tx, rx) = oneshot::channel();
        self.tx.send(DeviceManagerCommand::Scan { transport_type, respond_to: tx }).await
            .map_err(|e| FlowGridError::transport(format!("mpsc send failed: {e}")))?;
        rx.await.map_err(|e| FlowGridError::transport(format!("oneshot recv failed: {e}")))?
    }

    pub async fn shutdown(&self) {
        let _ = self.tx.send(DeviceManagerCommand::Shutdown).await;
    }
}

// ---------------------------------------------------------------------------
// Builder
// ---------------------------------------------------------------------------

pub fn build_device_manager(
) -> (DeviceManagerClient, mpsc::Receiver<DeviceManagerCommand>, EventBus) {
    let (tx, rx) = mpsc::channel(256);
    let event_bus = EventBus::new();
    (DeviceManagerClient::new(tx), rx, event_bus)
}

// ---------------------------------------------------------------------------
// HID injection helper
// ---------------------------------------------------------------------------

fn inject_hid(
    hal: &dyn PlatformHal,
    keymapper: Option<&KeyMapper>,
    frame_type: u8,
    payload: &[u8],
    from_platform: Platform,
) -> Result<()> {
    let to_platform = Platform::Linux;
    match frame_type {
        0x01 => {
            // KEYDOWN
            let (key_code, modifiers) = crate::protocol_ffi::unmarshal_keydown(payload)?;
            let remapped_mods = crate::protocol_ffi::remap_modifiers(
                modifiers,
                from_platform as u8,
                to_platform as u8,
            );
            let mapped_key = if let Some(mapper) = keymapper {
                let ctx = RuleContext::Global; // TODO: query active window context
                mapper.lookup(from_platform, to_platform, key_code, &ctx)
                    .map(|r| r.to_key)
                    .unwrap_or(key_code)
            } else {
                key_code
            };
            hal.inject_key_down(mapped_key, remapped_mods)?;
        }
        0x02 => {
            // KEYUP
            let key_code = crate::protocol_ffi::unmarshal_keyup(payload)?;
            let mapped_key = if let Some(mapper) = keymapper {
                let ctx = RuleContext::Global;
                mapper.lookup(from_platform, to_platform, key_code, &ctx)
                    .map(|r| r.to_key)
                    .unwrap_or(key_code)
            } else {
                key_code
            };
            // Modifiers are tracked per-key via 0xE0-0xE7 frames on the
            // wire, so KEY_UP releases only the key itself.
            hal.inject_key_up(mapped_key, 0)?;
        }
        0x03 => {
            // MOUSE MOVE
            let (dx, dy) = crate::protocol_ffi::unmarshal_mousemove(payload)?;
            hal.inject_mouse_move(dx, dy)?;
        }
        0x04 => {
            // MOUSE BUTTON
            let (btn, state) = crate::protocol_ffi::unmarshal_mousebtn(payload)?;
            hal.inject_mouse_button(btn, state)?;
        }
        0x05 => {
            // SCROLL
            let (dy, dx) = crate::protocol_ffi::unmarshal_scroll(payload)?;
            hal.inject_scroll(dy, dx)?;
        }
        _ => {}
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::hal::PlatformHal;
    use std::sync::{Arc, Mutex};

    #[derive(Debug, Default)]
    struct MockHal {
        events: Mutex<Vec<String>>,
    }

    impl PlatformHal for MockHal {
        fn init(&mut self) -> Result<()> { Ok(()) }
        fn inject_key_down(&self, key_code: u16, modifiers: u8) -> Result<()> {
            self.events.lock().unwrap().push(format!("key_down:0x{key_code:04X}:0x{modifiers:02X}"));
            Ok(())
        }
        fn inject_key_up(&self, key_code: u16, modifiers: u8) -> Result<()> {
            self.events.lock().unwrap().push(format!("key_up:0x{key_code:04X}:0x{modifiers:02X}"));
            Ok(())
        }
        fn inject_mouse_move(&self, dx: i16, dy: i16) -> Result<()> {
            self.events.lock().unwrap().push(format!("mouse_move:{dx},{dy}"));
            Ok(())
        }
        fn inject_mouse_button(&self, button_id: u8, state: u8) -> Result<()> {
            self.events.lock().unwrap().push(format!("mouse_btn:{button_id},{state}"));
            Ok(())
        }
        fn inject_scroll(&self, delta_y: i16, delta_x: i16) -> Result<()> {
            self.events.lock().unwrap().push(format!("scroll:{delta_y},{delta_x}"));
            Ok(())
        }
        fn shutdown(&mut self) -> Result<()> { Ok(()) }
    }

    #[test]
    fn test_inject_hid_keydown_no_mapper() {
        let hal = MockHal::default();
        let payload = crate::protocol_ffi::payload_keydown(0x0041, 0).unwrap();
        inject_hid(&hal, None, 0x01, &payload, Platform::MacOS).unwrap();
        let events = hal.events.lock().unwrap();
        assert_eq!(events.len(), 1);
        assert_eq!(events[0], "key_down:0x0041:0x00");
    }

    #[test]
    fn test_inject_hid_keyup_with_mapper() {
        let hal = MockHal::default();
        let mut mapper = KeyMapper::new();
        mapper.load_yaml_str(r#"
version: "1.0"
rules:
  - fromOS: macos
    toOS: linux
    fromKey: 0x0041
    toKey: 0x0061
    modifiers: 0x00
    context: global
"#).unwrap();
        let payload = crate::protocol_ffi::payload_keyup(0x0041).unwrap();
        inject_hid(&hal, Some(&mapper), 0x02, &payload, Platform::MacOS).unwrap();
        let events = hal.events.lock().unwrap();
        assert_eq!(events.len(), 1);
        assert_eq!(events[0], "key_up:0x0061:0x00"); // remapped
    }

    #[test]
    fn test_inject_hid_keydown_no_rule_fallback() {
        let hal = MockHal::default();
        let mut mapper = KeyMapper::new();
        mapper.load_yaml_str(r#"
version: "1.0"
rules:
  - fromOS: windows
    toOS: linux
    fromKey: 0x0041
    toKey: 0x0061
    modifiers: 0x00
    context: global
"#).unwrap();
        // macOS 'A' has no rule, should fall through with original key code
        let payload = crate::protocol_ffi::payload_keydown(0x0041, 0).unwrap();
        inject_hid(&hal, Some(&mapper), 0x01, &payload, Platform::MacOS).unwrap();
        let events = hal.events.lock().unwrap();
        assert_eq!(events.len(), 1);
        assert_eq!(events[0], "key_down:0x0041:0x00"); // no remapping
    }

    #[test]
    fn test_inject_hid_mouse_move() {
        let hal = MockHal::default();
        let payload = crate::protocol_ffi::payload_mousemove(100, -50).unwrap();
        inject_hid(&hal, None, 0x03, &payload, Platform::Linux).unwrap();
        let events = hal.events.lock().unwrap();
        assert_eq!(events.len(), 1);
        assert_eq!(events[0], "mouse_move:100,-50");
    }

    #[test]
    fn test_inject_hid_mouse_button() {
        let hal = MockHal::default();
        let payload = crate::protocol_ffi::payload_mousebtn(1, 1).unwrap();
        inject_hid(&hal, None, 0x04, &payload, Platform::Linux).unwrap();
        let events = hal.events.lock().unwrap();
        assert_eq!(events.len(), 1);
        assert_eq!(events[0], "mouse_btn:1,1");
    }

    #[test]
    fn test_inject_hid_scroll() {
        let hal = MockHal::default();
        let payload = crate::protocol_ffi::payload_scroll(3, -1).unwrap();
        inject_hid(&hal, None, 0x05, &payload, Platform::Linux).unwrap();
        let events = hal.events.lock().unwrap();
        assert_eq!(events.len(), 1);
        assert_eq!(events[0], "scroll:3,-1");
    }

    #[test]
    fn test_inject_hid_unknown_frame_type() {
        let hal = MockHal::default();
        inject_hid(&hal, None, 0x99, b"unknown", Platform::Linux).unwrap();
        let events = hal.events.lock().unwrap();
        assert!(events.is_empty());
    }

    #[tokio::test]
    async fn test_device_manager_list_devices() {
        let (client, rx, event_bus) = build_device_manager();
        let _handle = DeviceManager::spawn(rx, event_bus, None, None);
        let devices = client.list_devices().await;
        assert!(devices.is_empty());
        client.shutdown().await;
    }

    #[tokio::test]
    async fn test_device_manager_connect_disconnect() {
        let (client, rx, event_bus) = build_device_manager();
        let _handle = DeviceManager::spawn(rx, event_bus, None, None);

        let addr: SocketAddr = "127.0.0.1:12345".parse().unwrap();
        let result = client.connect("test-device".to_string(), addr, TransportType::DirectLink).await;
        // DirectLinkTransport::connect may fail on localhost with no server, that's ok for this test
        // We just verify the command path works
        if result.is_ok() {
            let devices = client.list_devices().await;
            assert_eq!(devices.len(), 1);
            assert_eq!(devices[0].id, "test-device");

            let _ = client.disconnect("test-device".to_string()).await;
            let devices = client.list_devices().await;
            assert!(devices.is_empty());
        }
        client.shutdown().await;
    }

    #[tokio::test]
    async fn test_device_manager_with_hal() {
        let hal = Box::new(MockHal::default()) as Box<dyn PlatformHal>;
        let (client, rx, event_bus) = build_device_manager();
        let _handle = DeviceManager::spawn(rx, event_bus, Some(hal), None);
        let devices = client.list_devices().await;
        assert!(devices.is_empty());
        client.shutdown().await;
    }
}
