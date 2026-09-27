//! C ABI bridge: DTK (C++) UI  <->  FlowGrid Rust core.
//!
//! All functions are called from the Qt main thread. Data crosses the boundary
//! as UTF-8 JSON owned by the caller until `fg_core_free_string` releases it.
//! Events are pulled (not pushed) via `fg_core_poll_events` so no Rust callback
//! ever runs on a Qt thread.

use flowgrid_core::app_settings::AppSettings;
use flowgrid_core::capture::InputCapture;
use flowgrid_core::clipboard::ClipboardSync;
use flowgrid_core::device_manager::{DeviceManager, DeviceManagerClient};
use flowgrid_core::event_bus::FlowGridEvent;
use flowgrid_core::hal::PlatformHal;
use flowgrid_core::keymapper::KeyMapper;
use std::ffi::{c_char, c_int, CStr, CString};
use std::sync::{mpsc, Mutex, OnceLock};
use std::time::Duration;

#[derive(Clone, Debug)]
enum UiEvent {
    DevicesChanged,
    KeyMapChanged,
    LatencyUpdated,
    ScanStateChanged(bool),
    Error(String),
}

struct CoreState {
    runtime: tokio::runtime::Runtime,
    client: DeviceManagerClient,
    events_rx: Mutex<mpsc::Receiver<UiEvent>>,
    keymapper: std::sync::Arc<KeyMapper>,
    config: flowgrid_core::config_store::ConfigStore,
    settings: Mutex<AppSettings>,
}

static STATE: OnceLock<CoreState> = OnceLock::new();

static INPUT_CAPTURE: Mutex<Option<std::sync::Arc<InputCapture>>> = Mutex::new(None);
static CLIPBOARD_SYNC: Mutex<Option<ClipboardSync>> = Mutex::new(None);

/// Start the core services. `config_dir` may be NULL to use the platform
/// default (~/.config/flowgrid). Returns 0 on success, -1 if already started.
#[unsafe(no_mangle)]
pub extern "C" fn fg_core_start(config_dir: *const c_char) -> c_int {
    if STATE.get().is_some() {
        return -1;
    }

    tracing_subscriber::fmt()
        .with_env_filter(
            tracing_subscriber::EnvFilter::try_from_default_env()
                .unwrap_or_else(|_| tracing_subscriber::EnvFilter::new("info")),
        )
        .try_init()
        .ok();
    tracing::info!("FlowGrid deepin core starting");

    let runtime = tokio::runtime::Runtime::new().expect("failed to create tokio runtime");

    let config = match std::panic::catch_unwind(|| {
        if config_dir.is_null() {
            flowgrid_core::config_store::ConfigStore::new()
        } else {
            let dir = unsafe { CStr::from_ptr(config_dir) }
                .to_string_lossy()
                .into_owned();
            Ok(flowgrid_core::config_store::ConfigStore::with_path(
                std::path::PathBuf::from(dir),
            ))
        }
    }) {
        Ok(Ok(store)) => store,
        Ok(Err(e)) => {
            tracing::warn!("config store init failed: {e}, using in-memory defaults");
            flowgrid_core::config_store::ConfigStore::new_in_memory()
        }
        Err(_) => flowgrid_core::config_store::ConfigStore::new_in_memory(),
    };

    flowgrid_core::cert_manager::set_default_config_dir(
        config.settings_path().parent().unwrap().to_path_buf(),
    );

    let settings = config.load_settings().unwrap_or_default();

    let mut keymapper = KeyMapper::new();
    if let Ok(yaml) = config.load_keymap_yaml()
        && let Err(e) = keymapper.load_yaml_str(&yaml)
    {
        tracing::warn!("failed to parse keymap YAML: {e}");
    }
    let keymapper = std::sync::Arc::new(keymapper);

    // HAL (Linux uinput) — shared with the KDE client
    let hal: Box<dyn PlatformHal> = {
        let mut injector = flowgrid_core::hal::linux_input::LinuxInputInjector::new();
        if let Err(e) = injector.init() {
            tracing::warn!("uinput HAL init failed: {e}; HID injection will not work");
        }
        Box::new(injector)
    };

    // DeviceManager::spawn uses tokio::spawn internally, so it must run
    // inside the runtime context.
    let _runtime_guard = runtime.enter();
    let (client, rx, event_bus) = flowgrid_core::device_manager::build_device_manager();
    DeviceManager::spawn(rx, event_bus.clone(), Some(hal), Some(std::sync::Arc::clone(&keymapper)));

    let (events_tx, events_rx) = mpsc::channel::<UiEvent>();

    // Forward EventBus events to the pull queue.
    runtime.spawn(async move {
        let mut subscriber = event_bus.subscribe();
        while let Ok(event) = subscriber.recv().await {
            let ui_event = match event {
                FlowGridEvent::DeviceDiscovered { .. }
                | FlowGridEvent::DeviceStateChanged { .. }
                | FlowGridEvent::DeviceRemoved { .. } => UiEvent::DevicesChanged,
                FlowGridEvent::LatencyUpdated { .. } => UiEvent::LatencyUpdated,
                FlowGridEvent::KeyMapChanged => UiEvent::KeyMapChanged,
                FlowGridEvent::ScanStateChanged { scanning } => {
                    UiEvent::ScanStateChanged(scanning)
                }
                FlowGridEvent::Error { message } => UiEvent::Error(message),
                _ => continue,
            };
            let _ = events_tx.send(ui_event);
        }
    });

    // Clipboard sync runs whenever the setting allows it; toggled live via
    // fg_core_set_setting("clipboardSync").
    if settings.clipboard_sync_enabled {
        match ClipboardSync::start(client.clone(), runtime.handle().clone()) {
            Ok(sync) => {
                *CLIPBOARD_SYNC.lock().unwrap() = Some(sync);
            }
            Err(e) => tracing::warn!("clipboard sync unavailable: {e}"),
        }
    }

    if STATE
        .set(CoreState {
            runtime,
            client,
            events_rx: Mutex::new(events_rx),
            keymapper,
            config,
            settings: Mutex::new(settings),
        })
        .is_err()
    {
        return -1;
    }
    0
}

/// Drain pending UI events. Returns a JSON array (possibly empty) like
/// `[{"type":"devices_changed"},...]`; caller frees with fg_core_free_string.
#[unsafe(no_mangle)]
pub extern "C" fn fg_core_poll_events() -> *mut c_char {
    let Some(state) = STATE.get() else { return empty_json_array() };
    let rx = state.events_rx.lock().unwrap();
    let mut events = Vec::new();
    while let Ok(event) = rx.try_recv() {
        let value = match event {
            UiEvent::DevicesChanged => serde_json::json!({"type": "devices_changed"}),
            UiEvent::KeyMapChanged => serde_json::json!({"type": "keymap_changed"}),
            UiEvent::LatencyUpdated => serde_json::json!({"type": "latency_updated"}),
            UiEvent::ScanStateChanged(scanning) => {
                serde_json::json!({"type": "scan_state_changed", "scanning": scanning})
            }
            UiEvent::Error(message) => serde_json::json!({"type": "error", "message": message}),
        };
        events.push(value);
        if events.len() >= 64 {
            break; // bounds: UI drains at 100 ms, 64 per batch is plenty
        }
    }
    to_c_string(serde_json::to_string(&events).unwrap_or_else(|_| "[]".into()))
}

/// All known devices as a JSON array.
#[unsafe(no_mangle)]
pub extern "C" fn fg_core_get_devices() -> *mut c_char {
    let Some(state) = STATE.get() else { return empty_json_array() };
    let devices = state
        .runtime
        .block_on(state.client.list_devices())
        .into_iter()
        .map(|d| {
            serde_json::json!({
                "id": d.id,
                "name": d.name,
                "platform": d.platform.name(),
                "meta": format!("{} · {} · {}", d.platform.name(), d.transport_type,
                                if d.connected { "Connected" } else { "Idle" }),
                "latency": format!("{:.1}ms", d.latency_ms),
                "connected": d.connected,
                "transport": d.transport_type,
            })
        })
        .collect::<Vec<_>>();
    to_c_string(serde_json::to_string(&devices).unwrap_or_else(|_| "[]".into()))
}

/// Latency of the first connected device, "--" when none.
#[unsafe(no_mangle)]
pub extern "C" fn fg_core_get_latency() -> *mut c_char {
    let Some(state) = STATE.get() else { return to_c_string("--".into()) };
    let latency = state
        .runtime
        .block_on(state.client.list_devices())
        .into_iter()
        .find(|d| d.connected)
        .map(|d| format!("{:.1}ms", d.latency_ms))
        .unwrap_or_else(|| "--".into());
    to_c_string(latency)
}

/// Human-readable connection state ("Idle" / "Connected to <name>").
#[unsafe(no_mangle)]
pub extern "C" fn fg_core_get_connection_state() -> *mut c_char {
    let Some(state) = STATE.get() else { return to_c_string("Idle".into()) };
    let state_str = state
        .runtime
        .block_on(state.client.list_devices())
        .into_iter()
        .find(|d| d.connected)
        .map(|d| format!("Connected to {}", d.name))
        .unwrap_or_else(|| "Idle".into());
    to_c_string(state_str)
}

/// Key mapping rules as a JSON array.
#[unsafe(no_mangle)]
pub extern "C" fn fg_core_get_keymap_rules() -> *mut c_char {
    let Some(state) = STATE.get() else { return empty_json_array() };
    let rules = state
        .keymapper
        .all_rules()
        .into_iter()
        .map(|r| {
            serde_json::json!({
                "from_key": format!("0x{:04X}", r.from_key),
                "to_key": format!("0x{:04X}", r.to_key),
                "context": r.context.as_str(),
            })
        })
        .collect::<Vec<_>>();
    to_c_string(serde_json::to_string(&rules).unwrap_or_else(|_| "[]".into()))
}

/// All settings flags as a JSON object.
#[unsafe(no_mangle)]
pub extern "C" fn fg_core_get_settings() -> *mut c_char {
    let Some(state) = STATE.get() else { return to_c_string("{}".into()) };
    let s = state.settings.lock().unwrap();
    let value = serde_json::json!({
        "keyMapping": s.key_mapping_enabled,
        "clipboardSync": s.clipboard_sync_enabled,
        "mouseSmoothing": s.mouse_smoothing_enabled,
        "autoConnect": s.auto_connect_on_launch,
        "dtls": s.dtls_enabled,
    });
    to_c_string(serde_json::to_string(&value).unwrap_or_else(|_| "{}".into()))
}

/// Set one settings flag and persist. `key` must be one of the keys from
/// fg_core_get_settings; returns 0 on success, -1 on unknown key.
#[unsafe(no_mangle)]
pub extern "C" fn fg_core_set_setting(key: *const c_char, value: c_int) -> c_int {
    let Some(state) = STATE.get() else { return -1 };
    if key.is_null() {
        return -1;
    }
    let key = unsafe { CStr::from_ptr(key) }.to_string_lossy().into_owned();
    let enabled = value != 0;

    let mut changed = state.config.load_settings().unwrap_or_default();
    match key.as_str() {
        "keyMapping" => changed.key_mapping_enabled = enabled,
        "clipboardSync" => changed.clipboard_sync_enabled = enabled,
        "mouseSmoothing" => changed.mouse_smoothing_enabled = enabled,
        "autoConnect" => changed.auto_connect_on_launch = enabled,
        "dtls" => changed.dtls_enabled = enabled,
        _ => return -1,
    }
    if key == "clipboardSync" {
        let mut slot = CLIPBOARD_SYNC.lock().unwrap();
        if enabled {
            if slot.is_none()
                && let Ok(sync) =
                    ClipboardSync::start(state.client.clone(), state.runtime.handle().clone())
            {
                *slot = Some(sync);
            }
        } else if let Some(sync) = slot.take() {
            sync.stop();
        }
    }
    if state.config.save_settings(&changed).is_err() {
        tracing::warn!("failed to persist settings");
    }
    *state.settings.lock().unwrap() = changed;
    0
}

/// Trigger a NearLink scan.
#[unsafe(no_mangle)]
pub extern "C" fn fg_core_scan() {
    let Some(state) = STATE.get() else { return };
    let client = state.client.clone();
    state.runtime.spawn(async move {
        if let Err(e) = client
            .scan(flowgrid_core::transport::TransportType::NearLink)
            .await
        {
            tracing::warn!("scan failed: {e}");
        }
    });
}

/// Connect to a discovered device by id.
#[unsafe(no_mangle)]
pub extern "C" fn fg_core_connect(device_id: *const c_char) {
    let Some(state) = STATE.get() else { return };
    if device_id.is_null() {
        return;
    }
    let id = unsafe { CStr::from_ptr(device_id) }.to_string_lossy().into_owned();
    let client = state.client.clone();
    // Same demo address as the KDE bridge; discovery supplies real addresses later.
    let addr = "127.0.0.1:24801".parse().unwrap();
    state.runtime.spawn(async move {
        if let Err(e) = client
            .connect(id.clone(), addr, flowgrid_core::transport::TransportType::NearLink)
            .await
        {
            tracing::warn!("connect failed for {id}: {e}");
        }
    });
}

#[unsafe(no_mangle)]
pub extern "C" fn fg_core_disconnect(device_id: *const c_char) {
    device_action(device_id, |client, id| async move {
        client.disconnect(id).await
    });
}

#[unsafe(no_mangle)]
pub extern "C" fn fg_core_remove_device(device_id: *const c_char) {
    // The client API has no separate remove; removing = disconnecting
    // (same as the KDE bridge).
    device_action(device_id, |client, id| async move {
        client.disconnect(id).await
    });
}


/// Turn Host-side input capture on or off. Returns 0 on success, -1 when the
/// core is not started, -2 when capture cannot start (permissions / no device).
#[unsafe(no_mangle)]
pub extern "C" fn fg_core_set_capturing(enabled: c_int) -> c_int {
    let Some(state) = STATE.get() else { return -1 };
    let want = enabled != 0;
    let mut slot = INPUT_CAPTURE.lock().unwrap();
    let active = slot.as_ref().is_some_and(|c| c.is_capturing());
    if want == active {
        return 0;
    }
    if !want {
        if let Some(current) = slot.take() {
            current.stop();
        }
        return 0;
    }
    match InputCapture::start(state.client.clone(), state.runtime.handle().clone()) {
        Ok(capture) => {
            *slot = Some(capture);
            0
        }
        Err(e) => {
            tracing::warn!("capture start failed: {e}");
            -2
        }
    }
}

/// 1 when Host-side capture is running.
#[unsafe(no_mangle)]
pub extern "C" fn fg_core_get_capturing() -> c_int {
    INPUT_CAPTURE
        .lock()
        .unwrap()
        .as_ref()
        .is_some_and(|c| c.is_capturing()) as c_int
}

/// Free a string returned by any fg_core_* function.
#[unsafe(no_mangle)]
pub extern "C" fn fg_core_free_string(s: *mut c_char) {
    if !s.is_null() {
        unsafe { drop(CString::from_raw(s)) };
    }
}

/// Shutdown hook; currently a no-op because the UI process exits as a whole.
#[unsafe(no_mangle)]
pub extern "C" fn fg_core_stop() {
    // The tokio runtime is torn down with the process; DeviceManager tasks
    // are detached by design (same as the KDE client).
    let _ = Duration::ZERO;
}

fn device_action<F, Fut>(device_id: *const c_char, f: F)
where
    F: FnOnce(DeviceManagerClient, String) -> Fut + Send + 'static,
    Fut: std::future::Future<Output = flowgrid_core::error::Result<()>> + Send + 'static,
{
    let Some(state) = STATE.get() else { return };
    if device_id.is_null() {
        return;
    }
    let id = unsafe { CStr::from_ptr(device_id) }
        .to_string_lossy()
        .into_owned();
    let client = state.client.clone();
    state.runtime.spawn(async move {
        if let Err(e) = f(client, id.clone()).await {
            tracing::warn!("device action failed for {id}: {e}");
        }
    });
}

fn to_c_string(s: String) -> *mut c_char {
    match CString::new(s) {
        Ok(c) => c.into_raw(),
        Err(_) => CString::default().into_raw(),
    }
}

fn empty_json_array() -> *mut c_char {
    to_c_string("[]".into())
}
