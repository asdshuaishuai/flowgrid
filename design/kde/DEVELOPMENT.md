# FlowGrid KDE Plasma 客户端开发文档

## 1. 概述

FlowGrid Linux/KDE 客户端采用 **Rust + cxx-qt + QML** 技术栈实现。核心逻辑使用 Rust 编写（异步 tokio 运行时），UI 层通过 cxx-qt 桥接导出到 Qt 6 / QML。

- **核心语言**: Rust 2024 Edition (Rust 1.93.1+)
- **UI 框架**: Qt 6 / QML (通过 cxx-qt 桥接)
- **异步运行时**: Tokio
- **BLE 发现**: `bluer` (BlueZ D-Bus 绑定)
- **WiFi Direct P2P**: `zbus` (wpa_supplicant D-Bus 接口)
- **DTLS 1.3**: `openssl` + `openssl-sys` (系统 OpenSSL 3.x)
- **HID 注入**: Linux `uinput` (Rust `libc` 直接调用)
- **配置持久化**: `dirs` + YAML/JSON 文件
- **目标平台**: KDE Plasma 5.27+ / Plasma 6 (Linux x86_64)

---

## 2. 项目结构

```
kde/
├── CMakeLists.txt                  # 顶层 CMake (Go 协议构建 + Rust cargo 构建 + 安装)
├── rust/
│   ├── Cargo.toml                  # Workspace 定义 (3 crates)
│   ├── core/                       # flowgrid-core — 纯 Rust 核心库
│   │   ├── Cargo.toml
│   │   └── src/
│   │       ├── lib.rs              # 模块导出
│   │       ├── app_settings.rs     # AppSettings 配置结构
│   │       ├── config_store.rs     # 配置持久化 (YAML/JSON + dirs)
│   │       ├── device.rs           # Device 模型 + Platform 枚举
│   │       ├── device_manager.rs   # Tokio mpsc Actor 设备管理器
│   │       ├── error.rs            # FlowGridError 统一错误类型
│   │       ├── event_bus.rs        # Tokio broadcast 事件总线
│   │       ├── dbus.rs             # D-Bus 服务存根 (zbus)
│   │       ├── logger.rs           # tracing 初始化
│   │       ├── protocol_ffi.rs     # Go cgo FFI 安全封装
│   │       ├── latency_monitor.rs  # 延迟统计封装
│   │       ├── hal/
│   │       │   ├── mod.rs          # PlatformHal trait
│   │       │   ├── linux_input.rs  # uinput HID 注入实现
│   │       │   └── mouse_interpolator.rs # 鼠标插值 (Appendix C)
│   │       ├── keymapper/
│   │       │   ├── mod.rs
│   │       │   ├── mapping_table.rs # YAML 规则加载 + 查找表
│   │       │   └── context.rs      # RuleContext (Global/App/Game)
│   │       └── transport/
│   │           ├── mod.rs          # Transport trait + TransportType
│   │           ├── packet.rs       # 长度前缀帧编解码
│   │           ├── direct_link.rs  # TCP + UDP DirectLink 传输
│   │           └── near_link.rs    # BLE + WiFi Direct P2P + DTLS 1.3 + UDP
│   ├── cxx-qt-bridge/              # flowgrid-cxx-qt — cxx-qt 桥接 crate
│   │   ├── Cargo.toml
│   │   ├── build.rs                # cxx-qt-build 编译脚本
│   │   └── src/
│   │       └── lib.rs              # FlowGridBackend QML 单例 + qinvokable
│   │   └── qml/                    # QML 资源文件
│   │       ├── Main.qml            # 主窗口
│   │       ├── TrayPopup.qml       # 托盘弹出菜单
│   │       ├── AddDeviceWizard.qml # 添加设备向导
│   │       ├── LatencyMonitor.qml  # 延迟监控窗口
│   │       ├── KeyMapperView.qml   # 键位映射编辑器
│   │       ├── SettingsView.qml    # 设置窗口
│   │       └── components/         # QML 组件
│   │           ├── DeviceCard.qml
│   │           ├── LatencyBadge.qml
│   │           ├── StatusDot.qml
│   │           ├── StepIndicator.qml
│   │           ├── KeyCap.qml
│   │           └── ContextBadge.qml
│   └── app/                        # flowgrid-app — 可执行程序入口
│       ├── Cargo.toml
│       ├── build.rs                # cxx-qt-build 编译脚本
│       └── src/
│           └── main.rs             # 初始化流水线 + QApplication
├── resources/
│   ├── org.flowgrid.desktop        # .desktop 文件
│   ├── icons/flowgrid.svg          # 应用图标
│   ├── 99-flowgrid.rules           # udev 规则 (uinput 权限)
│   └── flowgrid.service            # systemd user 服务单元
└── build/                          # CMake 构建目录
```

---

## 3. 架构与数据流

### 3.1 五层架构映射

| 层级 | 实现 | 说明 |
|------|------|------|
| L1 UI Shell | QML + cxx-qt | 主窗口、托盘向导、延迟显示、键映射编辑器 |
| L2 KeyMapper | `keymapper::KeyMapper` | OS 感知键重映射，YAML 热重载 |
| L3 Transport | `NearLinkTransport` / `DirectLinkTransport` | BLE 发现、WiFi Direct、DTLS 1.3、UDP HID 帧发送 |
| L4 Platform HAL | `LinuxInputInjector` | uinput 统一 HID 捕获与注入 |
| L5 Hardware | WiFi / BLE 5.0 / Ethernet | 物理网络层 |

### 3.2 核心数据流

```
Host 键盘/鼠标 → LinuxInputInjector (EV_REL/EV_KEY 读取)
                ↓
        KeyMapper::lookup() (OS 感知重映射)
                ↓
        protocol_ffi::encode_frame() (Go FFI 封装)
                ↓
        Transport::send_frame() (UDP/TCP)
                ↓
        ────────────────────────────────
                ↓
        Transport::recv_frame() (UDP recv 或 DTLS decrypt)
                ↓
        protocol_ffi::decode_frame() (Go FFI 解封装)
                ↓
        DeviceManager::route_incoming_frame() → inject_hid()
                ↓
        KeyMapper::lookup() (目标平台 Linux 映射)
                ↓
        LinuxInputInjector::inject_* (uinput /dev/uinput)
                ↓
        Target 系统响应
```

### 3.3 进程内模块交互

```
┌──────────────────────────────────────────────┐
│               flowgrid-app                     │
│  ┌──────────┐    ┌──────────────────────────┐ │
│  │ QML UI   │◄──►│ FlowGridBackend          │ │
│  │ (Qt6)    │    │ (cxx-qt bridge)          │ │
│  └──────────┘    │  ┌────────────────────┐  │ │
│                  │  │ qinvokable methods │  │ │
│                  │  │ watch 缓存通道读取  │  │ │
│                  │  │ poll_events() 信号  │  │ │
│                  │  └────────────────────┘  │ │
│                  └──────────┬───────────────┘ │
│                             │                │
│  ┌──────────────────────────┴──────────────┐ │
│  │         flowgrid-core (tokio)           │ │
│  │  ┌────────────┐   ┌──────────────────┐  │ │
│  │  │DeviceManager│   │ EventBus (broadcast)│ │ │
│  │  │(mpsc Actor)│   └────────┬─────────┘  │ │
│  │  └─────┬──────┘            │            │ │
│  │        │                   │            │ │
│  │  ┌─────┴──────┐   ┌──────┴──────┐     │ │
│  │  │ Transport   │   │ LinuxInput  │     │ │
│  │  │ NearLink    │   │ Injector    │     │ │
│  │  │ DirectLink  │   │ (uinput)    │     │ │
│  │  └─────────────┘   └─────────────┘     │ │
│  │  ┌─────────────┐   ┌─────────────┐     │ │
│  │  │ KeyMapper   │   │ ConfigStore │     │ │
│  │  │ (YAML/Arc)  │   │ (dirs+YAML) │     │ │
│  │  └─────────────┘   └─────────────┘     │ │
│  └─────────────────────────────────────────┘ │
│                                              │
│  ┌─────────────────────────────────────────┐ │
│  │ Go Protocol FFI (c-archive)            │ │
│  │ protocol/cgo/libflowgrid_protocol.a     │ │
│  └─────────────────────────────────────────┘ │
└──────────────────────────────────────────────┘
```

---

## 4. 核心模块详解

### 4.1 DeviceManager (Tokio mpsc Actor)

`device_manager.rs` 实现一个基于 Tokio `mpsc` 通道的 Actor 模式：

- **命令类型**: `Connect`, `Disconnect`, `Scan`, `ListDevices`, `SendFrame`, `IncomingFrame`, `Shutdown`
- **状态机**: `Idle → Connecting → Connected → Reconnecting → Disconnecting → Idle`
- **心跳**: 每 2 秒发送一次 `0x08` 心跳帧，超时 6 秒触发重连
- **重连**: 指数退避，最多 5 次尝试
- **HID 注入**: 收到 `0x01..=0x05` 帧时，经 `KeyMapper` 查表后通过 `PlatformHal` 注入

```rust
pub struct DeviceManager {
    rx: mpsc::Receiver<DeviceManagerCommand>,
    devices: HashMap<String, DeviceConnection>,
    event_bus: EventBus,
    hal: Option<Box<dyn PlatformHal>>,
    keymapper: Option<Arc<KeyMapper>>,
}
```

构建时传入 HAL 和 KeyMapper：

```rust
let (client, rx, event_bus) = build_device_manager();
let _handle = DeviceManager::spawn(
    rx, event_bus,
    Some(Box::new(injector) as Box<dyn PlatformHal>),
    Some(Arc::clone(&keymapper)),
);
```

### 4.2 Transport 层

#### `Transport` Trait

```rust
pub trait Transport: Send + Sync {
    fn connect(&mut self, addr: SocketAddr) -> Result<()>;
    fn disconnect(&mut self) -> Result<()>;
    fn send_frame(&self, data: &[u8]) -> Result<()>;
    fn recv_frame(&self, buf: &mut [u8]) -> Result<usize>;
    fn is_connected(&self) -> bool;
    fn local_addr(&self) -> Option<SocketAddr>;
    fn hmac_key(&self) -> Option<Vec<u8>>; // NearLink DTLS-derived HMAC key
}
```

#### DirectLinkTransport (`direct_link.rs`)

- mDNS 发现（预留）
- TCP 控制通道 + UDP 数据通道
- `recv_frame` 从 UDP socket 读取原始 HID 帧

#### NearLinkTransport (`near_link.rs`) — 核心协议

完整实现 Protocol v1.0 第三章 NearLink 规范：

1. **BLE 发现**: 使用 `bluer` 扫描 BLE Service UUID `0x1850`，解析广播数据
2. **WiFi Direct P2P**: 通过 `zbus` 调用 `wpa_supplicant` D-Bus 接口 (`P2PFind`, `P2PStopFind`, `P2PPeers`)，动态发现接口路径
3. **WiFi 参数交换**: BLE GATT Characteristic `0x1853` 读写 (20 字节请求 / 8 字节响应)
4. **DTLS 1.3 握手**: `openssl` ECDH P-256，`TLS_AES_256_GCM_SHA384`；自动加载自签名证书，TOFU 指纹验证，握手后通过 RFC 5705 export_keying_material 导出每帧 HMAC 密钥
5. **DtlsSession**: 手动 BIO 配对 (`pump_read`/`pump_write`) + `SSL_read`/`SSL_write` 非阻塞循环，可选 HMAC 追加
6. **UDP 传输**: 加密后通过 UDP socket 发送，解密时从 socket 读取写入 BIO

```rust
pub struct DtlsSession {
    ssl: Ssl,
    read_bio: BioMemSlice,
    write_bio: BioMemSlice,
}
impl DtlsSession {
    pub fn encrypt(&self, data: &[u8]) -> Result<Vec<u8>>;
    pub fn decrypt(&self, data: &[u8]) -> Result<Vec<u8>>;
}
```

### 4.3 HID 注入 (Linux HAL)

`hal/linux_input.rs` 实现 `PlatformHal` trait：

- 打开 `/dev/uinput` (需要 `99-flowgrid.rules` udev 权限)
- `ioctl` 注册 `EV_KEY`, `EV_REL` 事件类型
- 注册 0..255 全部键码 + `REL_X`, `REL_Y`, `REL_WHEEL`, `REL_HWHEEL`
- `UI_DEV_CREATE` 创建虚拟设备 "FlowGrid Virtual Input"
- `write` 写入 `input_event` 结构体 + `SYN_REPORT`

HID Usage ID → Linux keycode 映射表覆盖常用键：A-Z、数字、功能键、方向键、控制键等。

### 4.4 KeyMapper 映射引擎

`keymapper/mapping_table.rs`：

- **规则格式**: YAML，`version: "1.0"`，规则列表含 `fromOS`, `toOS`, `fromKey`, `toKey`, `modifiers`, `context`
- **上下文**: `Global` / `App(String)` / `Game`
- **查找逻辑**: 先精确匹配 `context`，无匹配时回退 `Global`
- **存储**: `RwLock<HashMap<RuleKey, MappingRule>>`，支持原子替换

```yaml
version: "1.0"
rules:
  - fromOS: macos
    toOS: linux
    fromKey: 0x001E
    toKey: 0x001E
    modifiers: 0x00
    context: global
  - fromOS: macos
    toOS: linux
    fromKey: 0x001E
    toKey: 0x0020
    modifiers: 0x00
    context: app:com.example.app
```

### 4.5 Protocol FFI (Go 桥接)

`protocol/cgo/bridge.go` 编译为 `libflowgrid_protocol.a` (c-archive)，`protocol_ffi.rs` 提供安全封装：

- 帧编解码 (`encode_frame` / `decode_frame` / `encode_frame_with_hmac` / `decode_frame_with_hmac`)
- 载荷生成 (`payload_keydown`, `payload_mousemove`, ...)
- 载荷解包 (`unmarshal_keydown`, `unmarshal_mousemove`, ...)
- 延迟监控 (`LatencyMonitor`, `HeartbeatTracker`, `ReconnectState`, `SequenceTracker`, `FragmentReassembler`)
- BLE 广播编解码 (`ble_advertising_encode` / `ble_advertising_decode`)
- WiFi Direct 参数编解码 (`wifidirect_request_encode` / `wifidirect_response_encode`)

所有返回的 C 指针均通过 `fg_free_buffer` 释放，内存安全由 Rust 侧管理。

---

## 5. cxx-qt 桥接层

`cxx-qt-bridge/src/lib.rs` 定义 `FlowGridBackend` QML 单例：

### QML 信号 (`#[qsignal]`)

- `devices_changed`
- `device_state_changed(id, state)`
- `device_removed(id)`
- `latency_updated(id, latency_ms)`
- `keymap_changed`
- `error_occurred(message)`
- `scan_state_changed(scanning)`

### QML 可调方法 (`#[qinvokable]`)

| 方法 | 说明 |
|------|------|
| `scan_devices()` | 启动 NearLink BLE 扫描 |
| `connect_device(id)` | 连接指定设备 (硬编码 127.0.0.1:24801) |
| `disconnect_device(id)` | 断开设备 |
| `remove_device(id)` | 移除设备 |
| `get_devices()` | 返回 `Vec<DeviceQml>` (读取 watch 缓存) |
| `get_latency()` | 返回延迟字符串 (如 "2.1ms") |
| `get_connection_state()` | 返回连接状态字符串 |
| `get_keymap_rules()` | 返回键位规则列表 |
| `add_keymap_rule(rule)` | 添加规则 (预留) |
| `remove_keymap_rule(index)` | 删除规则 (预留) |
| `set_setting(key, value)` | 设置布尔开关 |
| `get_setting(key)` | 读取布尔开关 |
| `poll_events()` | 消费事件队列并发射信号 |

### 事件驱动架构

1. **Core → Bridge**: `EventBus` (tokio broadcast) 事件 → `mpsc` 通道 → `poll_events()` 消费 → Qt `#[qsignal]` 发射
2. **Bridge 缓存**: `tokio::sync::watch` 通道缓存设备列表、延迟、连接状态，`get_*()` 方法直接读取
3. **QML 侧**: `Timer` 每 16ms 调用 `poll_events()`，仅消费队列，不查询业务状态

---

## 6. 应用初始化流程

`app/src/main.rs` 初始化流水线：

1. **初始化日志**: `flowgrid_core::logger::init()` (tracing)
2. **创建 Tokio Runtime**: `tokio::runtime::Runtime::new()`，存储全局 `Handle`
3. **加载配置**: `ConfigStore::new()` → `load_settings()`
4. **初始化 KeyMapper**: 创建 `KeyMapper` → 加载 YAML 规则 → `Arc::new` 共享
5. **初始化 HAL**: `LinuxInputInjector::new()` → `init()` 创建 uinput 设备
6. **构建 DeviceManager**: `build_device_manager()` → `spawn(rx, event_bus, Some(hal), Some(keymapper))`
7. **初始化桥接全局**: `init_runtime`, `init_device_manager_client`, `init_key_mapper`, `init_watchers`, `init_settings_cache`
8. **事件转发**: 将 `EventBus` 事件转换为 `BridgeEvent` 写入 `mpsc` 通道
9. **缓存更新**: 1 秒定时任务，从 `DeviceManagerClient` 拉取设备列表 → `watch::Sender`
10. **启动 QML**: `QGuiApplication` + `QQmlApplicationEngine` → 加载 `Main.qml`

---

## 7. 构建与运行

### 7.1 依赖环境

| 依赖 | 版本 | 说明 |
|------|------|------|
| Rust | 1.93.1+ | Edition 2024 |
| Go | 1.26.0+ | 用于编译协议 cgo FFI |
| CMake | 3.20+ | 构建编排 |
| Qt6 | 6.11+ | Core, Quick, Network, Bluetooth, Gui, Qml |
| KF6 | (可选) | Kirigami, Config, StatusNotifierItem 等头文件 |
| OpenSSL | 3.x | DTLS 1.3 支持 |
| BlueZ | 5.x+ | BLE 支持 (Linux 蓝牙协议栈) |
| wpa_supplicant | 2.10+ | WiFi Direct P2P 支持 |

### 7.2 构建命令

```bash
# 1. 构建 Rust workspace (包含 Go FFI 自动链接)
cd kde/rust
cargo build --release --workspace

# 2. 构建 Go 协议库 (单独)
cd protocol/cgo
CGO_ENABLED=1 go build -buildmode=c-archive -o libflowgrid_protocol.a

# 3. CMake 全量构建 + 安装
cd kde
mkdir -p build && cmake -B build -S . -DCMAKE_INSTALL_PREFIX=/tmp/flowgrid-install
cmake --build build --target install
```

### 7.3 测试

```bash
cd kde/rust
# 运行所有测试
cargo test --workspace

# 静态检查
cargo clippy --workspace

# 仅核心库测试
cargo test -p flowgrid-core
```

当前测试覆盖：43 项全部通过。

### 7.4 运行

```bash
# 开发模式 (直接从源码加载 QML)
cd kde/rust
FLOWGRID_QML_PATH=../cxx-qt-bridge/qml/Main.qml cargo run -p flowgrid-app

# 安装后运行
/tmp/flowgrid-install/bin/flowgrid

# 需要 uinput 权限
sudo cp kde/resources/99-flowgrid.rules /etc/udev/rules.d/
sudo udevadm control --reload
```

### 7.5 systemd 服务

```bash
# 安装用户服务
systemctl --user enable --now /tmp/flowgrid-install/lib/systemd/user/flowgrid.service
```

---

## 8. 配置说明

### 8.1 配置目录

```
~/.config/flowgrid/
├── settings.yaml          # 应用设置
├── keymap.yaml            # 键位映射规则
├── devices.json           # 已知设备列表
└── logs/                  # 日志文件
```

### 8.2 设置项 (`settings.yaml`)

```yaml
preferred_transport: nearLink   # nearLink | directLink | auto
key_mapping_enabled: true
clipboard_sync_enabled: true
mouse_smoothing_enabled: false
auto_connect_on_launch: true
latency_display: true
```

### 8.3 键位映射 (`keymap.yaml`)

```yaml
version: "1.0"
rules:
  - fromOS: macos
    toOS: linux
    fromKey: 0x001E
    toKey: 0x001E
    modifiers: 0x00
    context: global
  - fromOS: macos
    toOS: linux
    fromKey: 0x001E
    toKey: 0x0020
    modifiers: 0x00
    context: app:com.example.app
```

- `fromOS` / `toOS`: `macos`, `windows`, `linux`, `android`, `ios`
- `context`: `global` / `app:bundle_id` / `game`
- `modifiers`: 位掩码，`0x01=Shift`, `0x02=Ctrl`, `0x04=Alt`, `0x08=Meta`

---

## 9. 设计系统 (KDE Breeze)

### 9.1 颜色

```qml
readonly property color ink:      "#171a1f"
readonly property color muted:    "#68707d"
readonly property color kdeBlue:  "#3daee9"
readonly property color green:    "#10a36f"
readonly property color red:      "#dc2626"
readonly property color amber:    "#d97706"
readonly property color line:     "#d9dee7"

// 延迟状态
readonly property color latencyGoodBg: "#e8fbf1"
readonly property color latencyGoodFg: "#07966c"
readonly property color latencyWarnBg: "#fef3c7"
readonly property color latencyWarnFg: "#d97706"
readonly property color latencyBadBg:  "#fee2e2"
readonly property color latencyBadFg:  "#dc2626"
```

### 9.2 排版

| 元素 | 字号 | 字重 | 颜色 |
|------|------|------|------|
| Titlebar | 12px | 700 | #eef1f4 |
| Window Title | 22px | 700 | ink |
| Device Name | 13px | 800 | ink |
| Meta Text | 11px | 400 | muted |
| Section Head | 12px | 800 | ink |
| Category | 11px | 700 | muted |
| Button | 12px | 700 | white/kdeBlue |
| Badge | 10px | 800 | white |
| Key Cap | 11px | 800 | ink |
| Table Header | 10px | 700 | muted |

### 9.3 间距与圆角

| 元素 | 圆角 | 内边距 |
|------|------|--------|
| Window | 6px | — |
| Card | 7px | 11px |
| Button | 3px | 6px 12px |
| Badge/Pill | 999px | 3px 10px |
| Icon | 4px / 8px | — |
| Content | — | 14px |
| Toggle | 34×18px | — |
| Status Dot | 7×7px | — |
| Key Cap | 6px | 3px 8px (min-width 34px) |
| Step Circle | 22×22px | — |

---

## 10. 屏幕规格

### 10.1 主窗口 (480-520px 宽度)

**Titlebar (30px)**:
- 背景: `#31363b` (Breeze Dark)
- 左侧: 应用图标 (16×16, 圆角 4px, KDE 蓝) + "FlowGrid"
- 中间: "Connections"
- 右侧: 最小化/最大化/关闭按钮
- 文字颜色: `#eef1f4`

**工具栏**:
- 左侧: 状态指示灯 (7×7, 2s 脉冲, 绿色) + 连接状态文本
- 右侧: "+ Add Device" 按钮 (28px 高, KDE 蓝背景, 圆角 3px)

**设备卡片**:
```
┌─────────────────────────────────────────┐
│ [Win]  Windows 11 Desktop      [2.1ms] │
│  42×42   NearLink · Connected    ⏻ 🗑   │
└─────────────────────────────────────────┘
```
- 背景渐变: `#fbfcfd` → `#eff2f6`
- 边框: `1px solid rgba(109,122,140,0.18)`
- Hover: `box-shadow: 0 2px 10px rgba(15,23,42,0.08)`
- 断开连接: opacity 0.55

### 10.2 系统托盘 (260-280px 宽度)

```
┌────────────────────────────────┐
│ FlowGrid          ● 2.4ms     │  头部
├────────────────────────────────┤
│ CONNECTION                     │
│ Windows 11 Desktop   NearLink │
│ Latency              2.4ms    │
│ + Add Device                  │
├────────────────────────────────┤
│ QUICK TOGGLES                 │
│ Key Mapping        [──● ON ]  │  34×18 切换
│ Clipboard Sync     [──● ON ]  │
│ Mouse Smoothing    [●── OFF]  │
│ Auto Route         [──● ON ]  │
├────────────────────────────────┤
│ Open Main Window    Ctrl+⇧+F  │
│ Settings                       │
├────────────────────────────────┤
│ Quit FlowGrid                  │  红色
└────────────────────────────────┘
```

- 背景: `rgba(247,249,251,0.95)` + 背景模糊
- 分隔线: `1px rgba(110,123,141,0.12)`
- Hover: `#f1f5f9`, Active: `#e2e8f0`

### 10.3 键位映射编辑器 (520-580px 宽度)

**工具栏**:
- 搜索输入框
- 筛选标签: All / Global / Apps / Game (26px 高, 圆角 999px)
  - 激活: KDE 蓝背景, 白色文字
  - 未激活: 白色背景, muted 文字, 边框 `#d9dee7`

**表格**:
- 固定表头: 10px 大写, `#5d6878`
- 行 Hover: `#f8fafc`
- 边框: `rgba(100,116,139,0.1-0.16)`
- 字体: 11px

**Key Caps**:
- 最小宽度 34px, 内边距 3px 8px
- 边框: `1px solid rgba(80,94,112,0.2)`
- 阴影: `0 1px 2px rgba(0,0,0,0.04)`
- 圆角: 6px

**Context Badges**:
- Global: bg `#e8f7ff`, fg `#0675b9`
- Apps: bg `#fff7e6`, fg `#d48806`
- Game: bg `#f3e8ff`, fg `#7c3aed`

### 10.4 设置 (420-480px 宽度)

- 设置列表: grid gap 1px, bg `rgba(109,122,140,0.14)`
- 行: 最小高度 42px, bg `rgba(255,255,255,0.9)`
- 切换: 34-36×18-20px
- 滑块: 4px 轨道, KDE 蓝滑块 (14px)
- 协议标签: 可点击, 循环 NearLink → DirectLink → Auto

**导航栏 (底部)**:
- 背景: `#fafafa`, 上边框: `#d9dee7`
- 按钮: 28px 高, 圆角 3px
- 主按钮: KDE 蓝
- 未保存指示: amber 色

### 10.5 添加设备向导 (400px)

三步流程:
- 步骤圆圈: 22×22px
- 扫描器: 旋转环动画
- 设备列表: 迷你卡片 + 选中高亮
- 成功: 绿色圆圈 + 对勾 + 详情行

---

## 11. 动画规格

| 动画 | 时长 | 类型 | QML 实现 |
|------|------|------|----------|
| 状态指示灯脉冲 | 2s | 无限循环 | SequentialAnimation on opacity |
| 托盘淡入 | 0.2s | ease | NumberAnimation on y + opacity |
| 扫描器旋转 | 1s | linear 无限 | RotationAnimation |
| 成功缩放 | 0.4s | ease | SpringAnimation on scale |
| 柱状图 | 0.4s | ease | Behavior on height |
| 提示条 | 0.3s | ease | NumberAnimation |
| 切换 | 0.2s | ease | Behavior on x |
| 按钮激活 | 0.15s | ease | ScaleAnimator + ColorAnimation |

---

## 12. 已知限制与后续方向

### 当前限制

1. **WiFi Direct P2P**: 依赖 `wpa_supplicant` D-Bus 接口，某些发行版可能需要额外配置
2. **DTLS 证书**: 首次启动自动生成 ECDSA P-256 自签名证书，持久化到 `~/.config/flowgrid/cert.pem` / `key.pem`；采用 TOFU（首次信任）指纹模型，已连接的 Peer 指纹保存在 `trusted_peers.json`
3. **窗口焦点上下文**: `inject_hid` 中 `RuleContext` 固定为 `Global`，未实现基于当前激活窗口的上下文查询
4. **DirectLink mDNS**: 发现部分尚未实现
5. **剪切板同步**: 架构已预留，未实现具体协议

### 后续方向

- 端到端集成测试 (两台机器联调)
- 性能基准测试 (延迟 < 3ms / < 0.5ms 目标)
- 完善开发者指南与部署手册
- 实现基于窗口焦点的动态键位映射上下文
- 添加系统托盘集成 (KStatusNotifierItem)

---

## 13. 文件清单

| 文件 | 说明 |
|------|------|
| `kde/rust/core/src/lib.rs` | 核心模块导出 |
| `kde/rust/core/src/device_manager.rs` | Tokio Actor 设备管理器 |
| `kde/rust/core/src/transport/mod.rs` | Transport trait 定义 |
| `kde/rust/core/src/transport/near_link.rs` | NearLink 完整实现 (BLE + P2P + DTLS + UDP) |
| `kde/rust/core/src/transport/direct_link.rs` | DirectLink TCP+UDP 实现 |
| `kde/rust/core/src/hal/linux_input.rs` | uinput HID 注入 |
| `kde/rust/core/src/keymapper/mapping_table.rs` | YAML 键位映射引擎 |
| `kde/rust/core/src/protocol_ffi.rs` | Go FFI 安全封装 |
| `kde/rust/cxx-qt-bridge/src/lib.rs` | QML 后端单例 (cxx-qt) |
| `kde/rust/app/src/main.rs` | 应用入口 + 初始化流水线 |
| `kde/CMakeLists.txt` | CMake 构建配置 |
| `kde/resources/99-flowgrid.rules` | udev 权限规则 |
| `kde/resources/flowgrid.service` | systemd 用户服务 |
| `protocol/cgo/bridge.go` | Go 协议 cgo FFI 桥接 |
| `design/PROTOCOL.md` | 协议规范 (v1.0) 权威参考 |

---

*文档版本: 2026-06-25*  
*对应代码版本: Rust 1.93.1, Go 1.26.0, Qt 6.11.1, Protocol v1.0*
