## 1. Protocol Security Fixes

- [x] 1.1 Fix BLE discovery to read `service_data` instead of `manufacturer_data` (Apple ID 0x004C)
- [x] 1.2 Fix DTLS version constant from `TLS1_3` to `DTLS1_3` in `near_link.rs`
- [x] 1.3 Implement self-signed ECDSA P-256 certificate generation on first launch (`cert.pem`, `key.pem` in `~/.config/flowgrid/`)
- [x] 1.4 Implement TOFU fingerprint trust model: compute SHA-256 fingerprint on first connect, store in `trusted_peers.json`, verify on reconnect
- [x] 1.5 Add DTLS certificate loading to `init_dtls_context()` in `near_link.rs` (server cert + private key + peer fingerprint verification)
- [x] 1.6 Add per-frame 4B HMAC-SHA256 truncation to `protocol/frame.go` `EncodeBytes()`
- [x] 1.7 Add HMAC verification to `protocol/frame.go` `DecodeFrameBytes()`
- [x] 1.8 Derive HMAC key via `SSL_export_keying_material` (RFC 5705) from DTLS session after handshake
- [x] 1.9 Add payload length bounds check (`<= 255`) in `protocol_ffi.rs::encode_frame()`
- [x] 1.10 Add `SAFETY` comments to all `unsafe` blocks in `near_link.rs` and `protocol_ffi.rs`
- [x] 1.11 Write/update tests for BLE discovery, DTLS handshake, HMAC roundtrip, certificate generation

## 2. HID Injection Architecture Fixes

- [x] 2.1 Add `modifiers: u8` parameter to `PlatformHal::inject_key_down` and `inject_key_up` trait methods
- [x] 2.2 Implement modifier key injection in `LinuxInputInjector` via `EV_KEY` events for LCtrl/LShift/LAlt/LMeta bits
- [x] 2.3 Add modifier state tracking per-device in `DeviceManager` to avoid re-injecting held modifiers
- [x] 2.4 Call `remap_modifiers(from_platform, to_platform, modifiers)` in `inject_hid` before HAL injection
- [x] 2.5 Update all `PlatformHal` implementations and test mocks to match new trait signature
- [x] 2.6 Write tests for modifier remapping across macOS→Linux and Windows→macOS scenarios

## 3. Runtime Stability Fixes

- [x] 3.1 Fix `main.rs` config initialization: remove second `ConfigStore::new().unwrap()` panic path, use temp dir fallback
- [x] 3.2 Fix `DeviceManager` reconnect blocking: use `reconnect_deadline` (non-blocking `Instant` check) instead of `await sleep` in `select!` loop
- [x] 3.3 Fix `near_link.rs` SSL pointer: replace `std::mem::transmute_copy(&ssl)` with `ssl.as_ptr()`
- [x] 3.4 Add `SAFETY` comment to `linux_input.rs` `std::mem::forget(file)` explaining fd ownership
- [x] 3.5 Fix `DirectLinkTransport` double-send: remove TCP send from `send_frame()` (UDP-only)
- [x] 3.6 Fix latency timestamp to use `Instant::elapsed()` (monotonic clock) instead of `SystemTime::now()`
- [x] 3.7 Write tests for reconnect non-blocking, config initialization, and DTLS pointer safety

## 4. QML UI Data Binding Fixes

- [x] 4.1 Add `hide()` method to `SettingsView.qml`, `TrayPopup.qml`, `LatencyMonitor.qml`, `AddDeviceWizard.qml`
- [x] 4.2 Fix Loader secondary open: change `onClicked: loader.active = true` to `if (loader.item) loader.item.show(); else loader.active = true`
- [x] 4.3 Bind `SettingsView.qml` settings to `backend.get_setting()` / `backend.set_setting()` — remove hardcoded model array
- [x] 4.4 Bind `KeyMapperView.qml` rules to `backend.get_keymap_rules()` — remove hardcoded model array
- [x] 4.5 Bind `TrayPopup.qml` device info to `backend.get_devices()` — remove hardcoded device data
- [x] 4.6 Bind `TrayPopup.qml` toggles to `backend.get_setting()` — remove hardcoded on/off values
- [x] 4.7 Bind `AddDeviceWizard.qml` device list to `backend.scan_devices()` and `devices_changed` signal
- [x] 4.8 Bind `LatencyMonitor.qml` data to `backend` latency signals and `get_latency()` — remove mock Timer
- [x] 4.9 Implement `Main.qml` signal responses: `onLatencyUpdated` refresh device card, `onErrorOccured` show Toast/Popup, `onScanStateChanged` update indicator
- [x] 4.10 Implement `cxx-qt-bridge/src/lib.rs` `add_keymap_rule` and `remove_keymap_rule` (update `KEY_MAPPER` and emit `keymap_changed`)
- [x] 4.11 Fix `DeviceManager` `latency_ms` field update in `route_incoming_frame` (write `ms` to `conn.device.latency_ms`)
- [x] 4.12 Fix `main.rs` keymap watch channel (`keymap_tx`) to actually be used (remove `_` binding, add periodic update task)

## 5. Cross-Cutting & Verification

- [x] 5.1 Run `cargo test --workspace` and verify all tests pass (including new tests)
- [x] 5.2 Run `cargo clippy --workspace` and verify zero warnings
- [x] 5.3 Run `cargo build --release --workspace` and verify successful compilation
- [x] 5.4 Verify CMake build + install: `cmake --build build --target install`
- [x] 5.5 Verify QML files syntax (no undefined method references, no hardcoded data)
- [x] 5.6 Update `DEVELOPMENT.md` known limitations section to remove fixed items
- [x] 5.7 Archive change: run `openspec archive --change fix-p0-review-issues`
