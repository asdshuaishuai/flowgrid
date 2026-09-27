use cxx_qt_lib::QString;
use cxx_qt_lib::QGuiApplication;
use cxx_qt_lib::QQmlApplicationEngine;
use cxx_qt_lib::QUrl;
use flowgrid_core::hal::PlatformHal;
use std::path::PathBuf;
use std::sync::Arc;
use tokio::time::{interval, Duration};
use tracing::info;

fn resolve_qml_path() -> Option<PathBuf> {
    if let Ok(env_path) = std::env::var("FLOWGRID_QML_PATH") {
        let p = PathBuf::from(env_path);
        if p.exists() {
            return Some(p);
        }
    }

    if let Ok(exe_path) = std::env::current_exe()
        && let Some(exe_dir) = exe_path.parent()
    {
        let installed = exe_dir
            .join("..")
            .join("share")
            .join("flowgrid")
            .join("qml")
            .join("Main.qml");
        if installed.exists() {
            return Some(installed);
        }
    }

    let dev_path = PathBuf::from(env!("CARGO_MANIFEST_DIR"))
        .parent()
        .unwrap()
        .join("cxx-qt-bridge")
        .join("qml")
        .join("Main.qml");
    if dev_path.exists() {
        return Some(dev_path);
    }

    None
}

fn main() {
    flowgrid_core::logger::init();
    info!("FlowGrid starting up...");

    // Create tokio runtime for core services
    let runtime = tokio::runtime::Runtime::new().expect("Failed to create tokio runtime");
    let handle = runtime.handle().clone();
    flowgrid_cxx_qt::init_runtime(handle.clone());

    // 1. Initialize config store and load settings
    let config_store = match flowgrid_core::config_store::ConfigStore::new() {
        Ok(store) => {
            info!("Config store initialized");
            store
        }
        Err(e) => {
            tracing::warn!("Failed to initialize config store: {e}, using in-memory defaults");
            flowgrid_core::config_store::ConfigStore::new_in_memory()
        }
    };

    flowgrid_core::cert_manager::set_default_config_dir(config_store.settings_path().parent().unwrap().to_path_buf());

    let app_settings = match config_store.load_settings() {
        Ok(s) => s,
        Err(e) => {
            tracing::warn!("Failed to load settings: {e}, using defaults");
            flowgrid_core::app_settings::AppSettings::default()
        }
    };

    // 2. Initialize keymapper and load YAML rules
    let mut keymapper = flowgrid_core::keymapper::KeyMapper::new();
    if let Ok(yaml) = config_store.load_keymap_yaml()
        && let Err(e) = keymapper.load_yaml_str(&yaml)
    {
        tracing::warn!("Failed to parse keymap YAML: {e}");
    }
    let keymapper = Arc::new(keymapper);
    flowgrid_cxx_qt::init_key_mapper(Arc::clone(&keymapper));

    // 3. Initialize HAL (Linux uinput)
    let hal: Box<dyn flowgrid_core::hal::PlatformHal> = {
        let mut injector = flowgrid_core::hal::linux_input::LinuxInputInjector::new();
        if let Err(e) = injector.init() {
            tracing::warn!("Failed to initialize uinput HAL: {e}. HID injection will not work.");
        }
        Box::new(injector)
    };

    // 4. Build device manager with HAL and keymapper
    let (client, rx, event_bus) = flowgrid_core::device_manager::build_device_manager();
    let _device_manager = flowgrid_core::device_manager::DeviceManager::spawn(
        rx,
        event_bus.clone(),
        Some(hal),
        Some(Arc::clone(&keymapper)),
    );
    flowgrid_cxx_qt::init_device_manager_client(client.clone());

    // 5. Initialize bridge watch channels
    let (device_tx, device_rx) = tokio::sync::watch::channel(Vec::<flowgrid_core::device::Device>::new());
    let (keymap_tx, keymap_rx) = tokio::sync::watch::channel(Vec::<flowgrid_cxx_qt::KeyMapRuleQml>::new());
    let (latency_tx, latency_rx) = tokio::sync::watch::channel(String::from("--"));
    let (conn_state_tx, conn_state_rx) = tokio::sync::watch::channel(String::from("Idle"));

    flowgrid_cxx_qt::init_watchers(device_rx, keymap_rx, latency_rx, conn_state_rx);

    // Initialize settings cache from loaded settings
    let mut settings_map = std::collections::HashMap::new();
    settings_map.insert("keyMapping".to_string(), app_settings.key_mapping_enabled);
    settings_map.insert("clipboardSync".to_string(), app_settings.clipboard_sync_enabled);
    settings_map.insert("mouseSmoothing".to_string(), app_settings.mouse_smoothing_enabled);
    settings_map.insert("autoConnect".to_string(), app_settings.auto_connect_on_launch);
    settings_map.insert("dtls".to_string(), app_settings.dtls_enabled);
    flowgrid_cxx_qt::init_settings_cache(settings_map);
    flowgrid_cxx_qt::init_config_dir(config_store.settings_path().parent().unwrap().to_path_buf());

    // 6. Initialize the cross-thread event channel (core -> Qt bridge)
    let event_tx = flowgrid_cxx_qt::init_event_channel();

    // 7. Spawn a task that forwards EventBus events to the Qt bridge channel
    let event_tx_clone = event_tx.clone();
    handle.spawn(async move {
        let mut subscriber = event_bus.subscribe();
        while let Ok(event) = subscriber.recv().await {
            let bridge_event = match event {
                flowgrid_core::event_bus::FlowGridEvent::DeviceDiscovered { .. } => {
                    flowgrid_cxx_qt::BridgeEvent::DevicesChanged
                }
                flowgrid_core::event_bus::FlowGridEvent::DeviceStateChanged { id, state } => {
                    flowgrid_cxx_qt::BridgeEvent::DeviceStateChanged {
                        id,
                        state: state.to_string(),
                    }
                }
                flowgrid_core::event_bus::FlowGridEvent::DeviceRemoved { id } => {
                    flowgrid_cxx_qt::BridgeEvent::DeviceRemoved { id }
                }
                flowgrid_core::event_bus::FlowGridEvent::LatencyUpdated {
                    id,
                    latency_ms,
                    jitter_ms: _,
                } => {
                    flowgrid_cxx_qt::BridgeEvent::LatencyUpdated { id, latency_ms }
                }
                flowgrid_core::event_bus::FlowGridEvent::KeyMapChanged => {
                    flowgrid_cxx_qt::BridgeEvent::KeyMapChanged
                }
                flowgrid_core::event_bus::FlowGridEvent::Error { message } => {
                    flowgrid_cxx_qt::BridgeEvent::Error { message }
                }
                flowgrid_core::event_bus::FlowGridEvent::ScanStateChanged { scanning } => {
                    flowgrid_cxx_qt::BridgeEvent::ScanStateChanged { scanning }
                }
                _ => continue,
            };
            let _ = event_tx_clone.send(bridge_event);
        }
    });

    // 8. Spawn a task that periodically updates bridge caches from DeviceManager
    let keymapper_for_tick = Arc::clone(&keymapper);
    handle.spawn(async move {
        let mut tick = interval(Duration::from_secs(1));
        let client = client.clone();
        loop {
            tick.tick().await;

            // Update device list
            let devices = client.list_devices().await;
            let _ = device_tx.send(devices);

            // Update keymap rules
            let rules: Vec<flowgrid_cxx_qt::KeyMapRuleQml> = keymapper_for_tick
                .all_rules()
                .into_iter()
                .map(|r| flowgrid_cxx_qt::KeyMapRuleQml {
                    from_key: QString::from(&format!("0x{:04X}", r.from_key)),
                    to_key: QString::from(&format!("0x{:04X}", r.to_key)),
                    context: QString::from(&r.context.as_str()),
                })
                .collect();
            let _ = keymap_tx.send(rules);

            // Update latency string
            let mut latency_str = "--".to_string();
            for d in client.list_devices().await {
                if d.connected {
                    latency_str = format!("{:.1}ms", d.latency_ms);
                    break;
                }
            }
            let _ = latency_tx.send(latency_str);

            // Update connection state
            let mut state_str = "Idle".to_string();
            for d in client.list_devices().await {
                if d.connected {
                    state_str = format!("Connected to {}", d.name);
                    break;
                }
            }
            let _ = conn_state_tx.send(state_str);
        }
    });

    // 9. Start QML application
    let mut app = QGuiApplication::new();
    let mut engine = QQmlApplicationEngine::new();

    let qml_path = resolve_qml_path()
        .expect("Main.qml not found. Set FLOWGRID_QML_PATH or ensure QML files are installed.");
    let qml_str = QString::from(&*qml_path.to_string_lossy());
    let url = QUrl::from_local_file(&qml_str);

    if let Some(engine_pin) = engine.as_mut() {
        engine_pin.load(&url);
    }

    if let Some(app_pin) = app.as_mut() {
        app_pin.exec();
    }
}
