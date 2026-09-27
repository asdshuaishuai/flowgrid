// cxx-qt bridge: exports Rust objects to QML with event-driven signals and real backend integration.
use cxx_qt_lib::QString;
use std::pin::Pin;
use std::sync::{mpsc, Mutex, OnceLock};
use tokio::runtime::Handle;
use tokio::sync::watch;

/// Global event channel from core to Qt bridge.
static EVENT_QUEUE: OnceLock<Mutex<mpsc::Receiver<BridgeEvent>>> = OnceLock::new();

/// Global references to core services (initialized by app/src/main.rs).
static RUNTIME_HANDLE: OnceLock<Handle> = OnceLock::new();
static DEVICE_MANAGER_CLIENT: OnceLock<flowgrid_core::device_manager::DeviceManagerClient> = OnceLock::new();
static KEY_MAPPER: OnceLock<std::sync::Arc<flowgrid_core::keymapper::KeyMapper>> = OnceLock::new();
static CONFIG_DIR: OnceLock<std::sync::Mutex<std::path::PathBuf>> = OnceLock::new();

/// Cached state watch channels (core -> bridge).
static DEVICE_WATCH_RX: OnceLock<watch::Receiver<Vec<flowgrid_core::device::Device>>> = OnceLock::new();
static KEYMAP_WATCH_RX: OnceLock<watch::Receiver<Vec<KeyMapRuleQml>>> = OnceLock::new();
static LATENCY_WATCH_RX: OnceLock<watch::Receiver<String>> = OnceLock::new();
static CONN_STATE_WATCH_RX: OnceLock<watch::Receiver<String>> = OnceLock::new();
static SETTINGS_CACHE: OnceLock<Mutex<std::collections::HashMap<String, bool>>> = OnceLock::new();

/// Event types that can be pushed from core to QML.
#[derive(Clone, Debug)]
pub enum BridgeEvent {
    DevicesChanged,
    DeviceStateChanged { id: String, state: String },
    DeviceRemoved { id: String },
    LatencyUpdated { id: String, latency_ms: f64 },
    KeyMapChanged,
    Error { message: String },
    ScanStateChanged { scanning: bool },
}

/// Initialize the event channel. Returns the sender for the core side.
pub fn init_event_channel() -> mpsc::Sender<BridgeEvent> {
    let (tx, rx) = mpsc::channel();
    let _ = EVENT_QUEUE.set(Mutex::new(rx));
    tx
}

/// Initialize core service references (called from app/src/main.rs).
pub fn init_runtime(handle: Handle) {
    let _ = RUNTIME_HANDLE.set(handle);
}

pub fn init_device_manager_client(client: flowgrid_core::device_manager::DeviceManagerClient) {
    let _ = DEVICE_MANAGER_CLIENT.set(client);
}

pub fn init_key_mapper(mapper: std::sync::Arc<flowgrid_core::keymapper::KeyMapper>) {
    let _ = KEY_MAPPER.set(mapper);
}

pub fn init_config_dir(path: std::path::PathBuf) {
    let _ = CONFIG_DIR.set(std::sync::Mutex::new(path));
}

pub fn init_watchers(
    device_rx: watch::Receiver<Vec<flowgrid_core::device::Device>>,
    keymap_rx: watch::Receiver<Vec<KeyMapRuleQml>>,
    latency_rx: watch::Receiver<String>,
    conn_state_rx: watch::Receiver<String>,
) {
    let _ = DEVICE_WATCH_RX.set(device_rx);
    let _ = KEYMAP_WATCH_RX.set(keymap_rx);
    let _ = LATENCY_WATCH_RX.set(latency_rx);
    let _ = CONN_STATE_WATCH_RX.set(conn_state_rx);
}

pub fn init_settings_cache(settings: std::collections::HashMap<String, bool>) {
    let _ = SETTINGS_CACHE.set(Mutex::new(settings));
}

#[cxx_qt::bridge]
mod ffi {
    unsafe extern "C++" {
        include!("cxx-qt-lib/qstring.h");
        type QString = cxx_qt_lib::QString;
    }

    extern "RustQt" {
        #[qobject]
        #[qml_element]
        type FlowGridBackend = super::FlowGridBackendRust;

        #[qsignal]
        fn devices_changed(self: Pin<&mut Self>);

        #[qsignal]
        fn device_state_changed(self: Pin<&mut Self>, id: QString, state: QString);

        #[qsignal]
        fn device_removed(self: Pin<&mut Self>, id: QString);

        #[qsignal]
        fn latency_updated(self: Pin<&mut Self>, id: QString, latency_ms: f64);

        #[qsignal]
        fn keymap_changed(self: Pin<&mut Self>);

        #[qsignal]
        fn error_occurred(self: Pin<&mut Self>, message: QString);

        #[qsignal]
        fn scan_state_changed(self: Pin<&mut Self>, scanning: bool);

        /// Poll events from the memory queue and emit signals.
        #[qinvokable]
        fn poll_events(self: Pin<&mut Self>);

        #[qinvokable]
        fn scan_devices(self: Pin<&mut Self>);

        #[qinvokable]
        fn connect_device(self: Pin<&mut Self>, device_id: QString);

        #[qinvokable]
        fn disconnect_device(self: Pin<&mut Self>, device_id: QString);

        #[qinvokable]
        fn remove_device(self: Pin<&mut Self>, device_id: QString);

        #[qinvokable]
        fn get_devices(self: &FlowGridBackend) -> Vec<DeviceQml>;

        #[qinvokable]
        fn get_latency(self: &FlowGridBackend) -> QString;

        #[qinvokable]
        fn get_connection_state(self: &FlowGridBackend) -> QString;

        #[qinvokable]
        fn get_keymap_rules(self: &FlowGridBackend) -> Vec<KeyMapRuleQml>;

        #[qinvokable]
        fn add_keymap_rule(self: Pin<&mut Self>, rule: KeyMapRuleQml);

        #[qinvokable]
        fn remove_keymap_rule(self: Pin<&mut Self>, index: i32);

        #[qinvokable]
        fn set_setting(self: Pin<&mut Self>, key: QString, value: bool);

        #[qinvokable]
        fn get_setting(self: &FlowGridBackend, key: QString) -> bool;

        #[qinvokable]
        fn save_settings(self: &FlowGridBackend) -> bool;
    }

    #[derive(Default)]
    struct DeviceQml {
        id: QString,
        name: QString,
        platform: QString,
        meta: QString,
        latency: QString,
        connected: bool,
        transport_type: QString,
    }

    #[derive(Default, Clone)]
    struct KeyMapRuleQml {
        from_key: QString,
        to_key: QString,
        context: QString,
    }
}

pub use ffi::{DeviceQml, KeyMapRuleQml};

#[derive(Default)]
pub struct FlowGridBackendRust {
    // No fields needed; all state is in watch channels or global OnceLock.
}

fn device_to_qml(d: &flowgrid_core::device::Device) -> DeviceQml {
    DeviceQml {
        id: QString::from(&d.id),
        name: QString::from(&d.name),
        platform: QString::from(d.platform.name()),
        meta: QString::from(&format!(
            "{} · {} · {}",
            d.platform.name(),
            d.transport_type,
            if d.connected { "Connected" } else { "Idle" }
        )),
        latency: QString::from(&format!("{:.1}ms", d.latency_ms)),
        connected: d.connected,
        transport_type: QString::from(&d.transport_type),
    }
}

impl ffi::FlowGridBackend {
    fn poll_events(mut self: Pin<&mut Self>) {
        let queue = match crate::EVENT_QUEUE.get() {
            Some(q) => q,
            None => return,
        };
        let rx = queue.lock().unwrap();
        while let Ok(event) = rx.try_recv() {
            match event {
                BridgeEvent::DevicesChanged => {
                    self.as_mut().devices_changed();
                }
                BridgeEvent::DeviceStateChanged { id, state } => {
                    self.as_mut().device_state_changed(
                        QString::from(&id),
                        QString::from(&state),
                    );
                }
                BridgeEvent::DeviceRemoved { id } => {
                    self.as_mut().device_removed(QString::from(&id));
                }
                BridgeEvent::LatencyUpdated { id, latency_ms } => {
                    self.as_mut().latency_updated(QString::from(&id), latency_ms);
                }
                BridgeEvent::KeyMapChanged => {
                    self.as_mut().keymap_changed();
                }
                BridgeEvent::Error { message } => {
                    self.as_mut().error_occurred(QString::from(&message));
                }
                BridgeEvent::ScanStateChanged { scanning } => {
                    self.as_mut().scan_state_changed(scanning);
                }
            }
        }
    }

    fn scan_devices(mut self: Pin<&mut Self>) {
        if let (Some(client), Some(handle)) = (DEVICE_MANAGER_CLIENT.get(), RUNTIME_HANDLE.get()) {
            let client = client.clone();
            handle.spawn(async move {
                if let Err(e) = client.scan(flowgrid_core::transport::TransportType::NearLink).await {
                    tracing::warn!("Scan failed: {e}");
                }
            });
            self.as_mut().scan_state_changed(true);
        } else {
            tracing::warn!("DeviceManager not initialized; scan is a no-op");
        }
    }

    fn connect_device(self: Pin<&mut Self>, device_id: QString) {
        if let (Some(client), Some(handle)) = (DEVICE_MANAGER_CLIENT.get(), RUNTIME_HANDLE.get()) {
            let id = device_id.to_string();
            let client = client.clone();
            let addr = "127.0.0.1:24801".parse().unwrap();
            handle.spawn(async move {
                if let Err(e) = client.connect(id.clone(), addr, flowgrid_core::transport::TransportType::NearLink).await {
                    tracing::warn!("Connect failed for {id}: {e}");
                }
            });
        } else {
            tracing::warn!("DeviceManager not initialized; connect is a no-op");
        }
    }

    fn disconnect_device(self: Pin<&mut Self>, device_id: QString) {
        if let (Some(client), Some(handle)) = (DEVICE_MANAGER_CLIENT.get(), RUNTIME_HANDLE.get()) {
            let id = device_id.to_string();
            let client = client.clone();
            handle.spawn(async move {
                if let Err(e) = client.disconnect(id).await {
                    tracing::warn!("Disconnect failed: {e}");
                }
            });
        } else {
            tracing::warn!("DeviceManager not initialized; disconnect is a no-op");
        }
    }

    fn remove_device(self: Pin<&mut Self>, device_id: QString) {
        if let (Some(client), Some(handle)) = (DEVICE_MANAGER_CLIENT.get(), RUNTIME_HANDLE.get()) {
            let id = device_id.to_string();
            let client = client.clone();
            handle.spawn(async move {
                if let Err(e) = client.disconnect(id).await {
                    tracing::warn!("Remove failed: {e}");
                }
            });
        } else {
            tracing::warn!("DeviceManager not initialized; remove is a no-op");
        }
    }

    fn get_devices(&self) -> Vec<DeviceQml> {
        if let Some(rx) = DEVICE_WATCH_RX.get() {
            let devices = rx.borrow();
            if !devices.is_empty() {
                return devices.iter().map(device_to_qml).collect();
            }
        }
        vec![]
    }

    fn get_latency(&self) -> QString {
        if let Some(rx) = LATENCY_WATCH_RX.get() {
            QString::from(&*rx.borrow())
        } else {
            QString::from("--")
        }
    }

    fn get_connection_state(&self) -> QString {
        if let Some(rx) = CONN_STATE_WATCH_RX.get() {
            QString::from(&*rx.borrow())
        } else {
            QString::from("Idle")
        }
    }

    fn get_keymap_rules(&self) -> Vec<KeyMapRuleQml> {
        if let Some(rx) = KEYMAP_WATCH_RX.get() {
            rx.borrow().clone()
        } else {
            vec![]
        }
    }

    fn add_keymap_rule(mut self: Pin<&mut Self>, rule: KeyMapRuleQml) {
        let from_key_str = rule.from_key.to_string();
        let to_key_str = rule.to_key.to_string();
        let context_str = rule.context.to_string();

        let from_key = u16::from_str_radix(from_key_str.trim_start_matches("0x"), 16).unwrap_or(0);
        let to_key = u16::from_str_radix(to_key_str.trim_start_matches("0x"), 16).unwrap_or(0);
        let context = flowgrid_core::keymapper::RuleContext::parse(&context_str);

        let new_rule = flowgrid_core::keymapper::MappingRule {
            from_os: "macos".to_string(),
            to_os: "linux".to_string(),
            from_key,
            to_key,
            modifiers: 0,
            context,
        };

        if let Some(mapper) = KEY_MAPPER.get() {
            if let Err(e) = mapper.add_rule(new_rule) {
                tracing::warn!("Failed to add keymap rule: {e}");
            } else {
                self.as_mut().keymap_changed();
            }
        }
    }

    fn remove_keymap_rule(mut self: Pin<&mut Self>, index: i32) {
        if index < 0 {
            return;
        }
        if let Some(mapper) = KEY_MAPPER.get() {
            if let Err(e) = mapper.remove_rule_by_index(index as usize) {
                tracing::warn!("Failed to remove keymap rule: {e}");
            } else {
                self.as_mut().keymap_changed();
            }
        }
    }

    fn set_setting(self: Pin<&mut Self>, key: QString, value: bool) {
        let key_str = key.to_string();
        tracing::info!("QML: set_setting {} = {}", key_str, value);
        if let Some(cache) = SETTINGS_CACHE.get() {
            let mut cache = cache.lock().unwrap();
            cache.insert(key_str, value);
        }
    }

    fn get_setting(&self, key: QString) -> bool {
        if let Some(cache) = SETTINGS_CACHE.get() {
            let cache = cache.lock().unwrap();
            cache.get(&key.to_string()).copied().unwrap_or(false)
        } else {
            match key.to_string().as_str() {
                "keyMapping" | "clipboardSync" | "dtls" | "autoConnect" => true,
                "mouseSmoothing" => false,
                _ => false,
            }
        }
    }

    fn save_settings(&self) -> bool {
        if let Some(cache) = SETTINGS_CACHE.get() {
            let cache = cache.lock().unwrap();
            let settings = flowgrid_core::app_settings::AppSettings {
                preferred_transport: flowgrid_core::app_settings::PreferredTransport::Auto,
                key_mapping_enabled: *cache.get("keyMapping").unwrap_or(&true),
                clipboard_sync_enabled: *cache.get("clipboardSync").unwrap_or(&true),
                mouse_smoothing_enabled: *cache.get("mouseSmoothing").unwrap_or(&false),
                auto_connect_on_launch: *cache.get("autoConnect").unwrap_or(&true),
                dtls_enabled: *cache.get("dtls").unwrap_or(&true),
            };
            if let Some(config_dir) = CONFIG_DIR.get() {
                let config_dir = config_dir.lock().unwrap().clone();
                let store = flowgrid_core::config_store::ConfigStore::with_path(config_dir);
                if let Err(e) = store.save_settings(&settings) {
                    tracing::warn!("Failed to save settings: {e}");
                    return false;
                }
                return true;
            }
        }
        false
    }
}
