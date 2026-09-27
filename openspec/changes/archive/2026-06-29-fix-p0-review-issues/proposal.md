## Why

经过全面对比审查，FlowGrid KDE 客户端在三个维度（UI 前端、业务逻辑、协议实现）共发现 28 项 P0 阻塞级问题。这些问题导致：
- **协议安全失效**：BLE 发现解析错误数据源、DTLS 无证书、HMAC 缺失，NearLink 可被中间人注入攻击
- **HID 功能完全不可用**：修饰符映射从未被调用，HAL 架构上不支持修饰符注入，跨平台键鼠映射失效
- **运行时崩溃/死锁**：QML 调用未定义方法、配置初始化二次 panic、Actor 重连阻塞整个事件循环
- **前端无数据**：所有子窗口均为硬编码假数据，无法与后端交互

这些问题不修复，应用无法进入可用状态。需要一次性集中修复。

## What Changes

- **修复 BLE 发现**：从 `manufacturer_data` (Apple ID) 改为读取 `service_data` 正确解析协议广播数据
- **修复 DTLS 安全层**：添加自签名 ECDSA P-256 证书生成、首次配对指纹信任机制、修复 DTLS 版本常量使用 DTLS1_3
- **添加帧 HMAC 验证**：`HIDFrame.EncodeBytes()` 追加 4B SHA-256 截断 HMAC，`DecodeFrameBytes()` 验证
- **修复 HID 修饰符架构**：`PlatformHal` trait 添加 `modifiers: u8` 参数，`inject_hid` 调用 `remap_modifiers()` 后通过 HAL 注入
- **修复运行时稳定性**：`main.rs` 配置初始化移除二次 panic、`DeviceManager` 重连逻辑拆分为独立 task 避免阻塞 `select!`
- **修复 `near_link.rs` SSL 指针**：`transmute_copy` → `ssl.as_ptr()` 消除 UB
- **修复 QML 全部数据绑定**：所有子窗口从硬编码改为读取后端 `watch` 缓存、`get_*()` 方法
- **修复 QML 运行时错误**：所有子窗口添加 `hide()` 方法、修复 Loader 二次打开失效
- **实现信号响应 UI**：`latency_updated` 刷新设备卡片、`error_occurred` 显示 Toast、`scan_state_changed` 更新指示器

## Capabilities

### New Capabilities
- `dtls-cert-management`: 自签名证书生成、指纹持久化、信任首次使用 (TOFU) 模型
- `frame-integrity-hmac`: 每帧 4B HMAC-SHA256 截断校验
- `modifier-hid-injection`: 跨平台修饰符映射 + uinput 注入

### Modified Capabilities
<!-- 本次为纯修复，无 spec-level 行为变更 — 修改限于实现层，不修改协议规范本身 -->
- _(无 spec 级行为变更，仅修复实现层缺陷)_

## Impact

- **Rust core**: `transport/near_link.rs`, `device_manager.rs`, `hal/mod.rs`, `hal/linux_input.rs`, `protocol_ffi.rs`, `app/src/main.rs`
- **Go protocol**: `protocol/frame.go`, `protocol/discovery.go` (HMAC 和广播数据)
- **QML 前端**: `Main.qml`, `SettingsView.qml`, `KeyMapperView.qml`, `TrayPopup.qml`, `AddDeviceWizard.qml`, `LatencyMonitor.qml`
- **cxx-qt bridge**: `cxx-qt-bridge/src/lib.rs` (add/remove_keymap_rule 实现、缓存更新)
- **测试**: 所有 P0 相关测试需要更新以验证修复
- **兼容性**: 无 API 破坏，纯修复，修复后协议实现与 `design/PROTOCOL.md` 一致
