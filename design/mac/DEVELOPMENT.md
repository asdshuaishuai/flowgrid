# FlowGrid macOS Native Development Spec

## Overview

FlowGrid macOS 原生客户端，基于 SwiftUI + Swift 6，采用 macOS 原生设计风格。以纯菜单栏应用运行，支持 NearLink (BLE+WiFi，主推) 和 DirectLink (TCP/mDNS，备用) 双协议。

## Tech Stack

| 项 | 选择 |
|---|---|
| UI Framework | SwiftUI (macOS 15+) |
| Language | Swift 6 (Strict Concurrency) |
| BLE | CoreBluetooth (CBCentralManager) |
| Network | Network.framework (NWConnection, NWBrowser) |
| HID Injection | CoreGraphics (CGEvent) |
| Tray | MenuBarExtra (.window style) |
| Config | UserDefaults + FileManager (YAML) |
| Build | XcodeGen + SPM |
| Target OS | macOS 15+ (Sequoia) |

## Architecture

```
FlowGrid.xcodeproj
├── FlowGridCore/                   — 核心层 SPM 包
│   ├── Sources/FlowGridCore/
│   │   ├── Transport/
│   │   │   ├── TransportProtocol.swift      — 传输层协议
│   │   │   ├── DeviceInfo.swift             — 设备信息模型
│   │   │   ├── ConnectionState.swift        — 连接状态枚举
│   │   │   ├── HIDFrame.swift               — 统一 HID 帧格式
│   │   │   ├── NearLinkTransport.swift      — NearLink 实现 (主推)
│   │   │   └── DirectLinkTransport.swift    — DirectLink 实现
│   │   ├── HAL/
│   │   │   ├── PlatformHAL.swift            — macOS CGEvent 注入
│   │   │   ├── MouseInterpolator.swift      — 鼠标平滑插值
│   │   │   └── EventTypes.swift             — UnifiedHIDEvent 等
│   │   ├── KeyMapping/
│   │   │   ├── KeyMapper.swift              — 键位映射引擎 (Actor)
│   │   │   ├── KeyRule.swift                — 映射规则模型
│   │   │   └── MappingTable.swift           — 映射表管理
│   │   ├── DeviceManager.swift              — 设备管理器 (Actor)
│   │   ├── LatencyMonitor.swift             — 延迟监控
│   │   └── Persistence/
│   │       └── ConfigStore.swift            — UserDefaults + 文件持久化
│   └── Tests/FlowGridCoreTests/
├── FlowGridUI/                     — UI 层 SPM 包
│   ├── Sources/FlowGridUI/
│   │   ├── App/
│   │   │   ├── FlowGridApp.swift            — @main 入口 + MenuBarExtra
│   │   │   └── AppDelegate.swift            — NSApplication 代理
│   │   ├── State/
│   │   │   ├── AppState.swift               — @Observable 全局状态
│   │   │   └── Route.swift                  — 导航路由
│   │   ├── StatusBar/
│   │   │   ├── StatusBarView.swift          — 菜单栏托盘内容
│   │   │   ├── StatusHeaderView.swift       — 托盘头部
│   │   │   └── QuickActionsView.swift       — 快捷操作
│   │   ├── Devices/
│   │   │   ├── DeviceManagerView.swift      — 主窗口设备管理
│   │   │   ├── DeviceCardView.swift         — 设备卡片
│   │   │   └── LatencyMonitorView.swift     — 延迟监控窗口
│   │   ├── Pairing/
│   │   │   ├── PairingWizard.swift          — 配对向导
│   │   │   ├── DeviceDiscoveryView.swift    — 步骤 1：发现
│   │   │   ├── PairingProgressView.swift    — 步骤 2：配对
│   │   │   └── PairingSuccessView.swift     — 步骤 3：成功
│   │   ├── KeyMapping/
│   │   │   ├── KeyMapperView.swift          — 键位映射编辑器
│   │   │   └── KeyRuleRow.swift             — 映射规则行
│   │   ├── Settings/
│   │   │   ├── SettingsView.swift           — 设置窗口
│   │   │   ├── NetworkSettingsView.swift    — 网络设置
│   │   │   └── DirectLinkSettingsView.swift — DirectLink 设置
│   │   └── Shared/
│   │       ├── Theme.swift                  — 设计系统
│   │       ├── Icons.swift                  — SF Symbols 封装
│   │       └── Components.swift             — 通用组件
│   └── Package.swift
├── FlowGrid/                       — App Target (组装层)
│   ├── Sources/FlowGrid/
│   │   ├── DependencyContainer.swift        — 依赖注入容器
│   │   └── Info.plist
│   └── FlowGrid.entitlements
└── project.yml                     — XcodeGen 配置
```

## Design System (macOS Native)

### Colors

```swift
enum FGColors {
    // Text
    static let ink = Color(hex: "171a1f")
    static let muted = Color(hex: "68707d")

    // Brand
    static let accent = Color(hex: "007aff")     // macOS system blue
    static let blue = Color(hex: "007aff")

    // Status
    static let green = Color(hex: "10a36f")
    static let red = Color(hex: "dc2626")
    static let amber = Color(hex: "d97706")

    // UI
    static let line = Color(hex: "d9dee7")
    static let background = Color(hex: "f5f5f7")
    static let cardBg = Color.white

    // Traffic Lights
    static let macOSRed = Color(hex: "ff5f57")
    static let macOSYellow = Color(hex: "febc2e")
    static let macOSGreen = Color(hex: "28c840")

    // Latency Badge
    static let latencyGoodBg = Color(hex: "e8fbf1")
    static let latencyGoodFg = Color(hex: "07966c")
    static let latencyWarnBg = Color(hex: "fef3c7")
    static let latencyWarnFg = Color(hex: "d97706")
    static let latencyBadBg = Color(hex: "fee2e2")
    static let latencyBadFg = Color(hex: "dc2626")

    // Badge / Pill
    static let badgeBg = Color(hex: "e8f7ff")
    static let badgeFg = Color(hex: "0675b9")

    // Toggle
    static let toggleOn = Color(hex: "34c759")    // macOS green
    static let toggleOff = Color(hex: "e5e5ea")

    // Tray background
    static let trayBg = Color(white: 1, opacity: 0.92)
}
```

### Typography

```swift
enum FGTypography {
    // SF Pro Text / SF Pro Display
    static let fontFamily = ".SF NS"

    static let titlebar = Font.system(size: 12, weight: .bold)
    static let deviceName = Font.system(size: 13, weight: .heavy)
    static let meta = Font.system(size: 11, weight: .regular)
    static let status = Font.system(size: 12, weight: .semibold)
    static let sectionHead = Font.system(size: 12, weight: .heavy)
    static let category = Font.system(size: 10, weight: .bold)  // uppercase
    static let button = Font.system(size: 12, weight: .bold)
    static let badge = Font.system(size: 10, weight: .heavy)
    static let h1 = Font.system(size: 22, weight: .bold)
    static let h3 = Font.system(size: 16, weight: .bold)
    static let heroLatency = Font.system(size: 42, weight: .heavy)
}
```

### Spacing & Sizing

```swift
enum FGSizing {
    static let windowRadius: CGFloat = 12
    static let cardRadius: CGFloat = 10
    static let buttonRadius: CGFloat = 6
    static let pillRadius: CGFloat = 999
    static let iconRadius: CGFloat = 10

    static let contentPadding: CGFloat = 14
    static let cardPadding: CGFloat = 11
    static let sectionHeadPadding: CGFloat = 9
    static let toggleWidth: CGFloat = 36
    static let toggleHeight: CGFloat = 20
    static let statusDotSize: CGFloat = 7
    static let stepCircleSize: CGFloat = 22
}

enum FGWindow {
    static let mainWidth: CGFloat = 520
    static let mainHeight: CGFloat = 560
    static let trayWidth: CGFloat = 320
    static let addDeviceWidth: CGFloat = 400
    static let addDeviceHeight: CGFloat = 440
    static let latencyWidth: CGFloat = 420
    static let latencyHeight: CGFloat = 480
}
```

## Screen Specifications

### 1. Menu Bar Tray (320px width)

```swift
// MenuBarExtra with .window style for custom UI
MenuBarExtra {
    StatusBarView()
        .environment(appState)
} label: {
    Image(systemName: appState.connectionState.menuBarIcon)
}
.menuBarExtraStyle(.window)
```

**Layout:**
```
┌──────────────────────────────────────────┐
│ FlowGrid              ● 2.4ms           │  12px 14px padding
│ ──────────────────────────────────────── │  Separator
│                                          │
│ DEVICES                    10px uppercase│
│ Windows 11 Desktop     Connected  2.1ms │  Device row
│ + Add Device                  blue button│
│ ──────────────────────────────────────── │
│ Quick Actions                            │
│ Key Mapping            [──● ON ] 36×20   │  macOS toggle
│ Clipboard Sync         [──● ON ]         │
│ Mouse Smoothing        [●── OFF]         │
│ ──────────────────────────────────────── │
│ Show Connections             ⌘1          │
│ Latency Monitor              ⌘2          │
│ Preferences…                 ⌘,          │
│ ──────────────────────────────────────── │
│ Quit FlowGrid                ⌘Q   red    │
└──────────────────────────────────────────┘
```

**Styling:**
- Background: rgba(255,255,255,0.92) + backdrop blur 24px + saturate 1.8
- Border radius: 14px
- Shadow: 0 20px 40px rgba(15,23,42,0.14)
- Animation: fadeIn 0.25s cubic-bezier(.16,1,.3,1)
- Status pill: bg #e8fbf1, fg #07966c, rounded 999px
- Toggle: macOS style (green #34c759 / gray #e5e5ea)
- Separator: rgba(110,123,141,0.12), 1px
- Hover: rgba(0,0,0,0.04)
- Shortcut text: #8e8e93, 12px

### 2. Main Window (520×560)

```swift
Window("FlowGrid", id: "main") {
    DeviceManagerView()
        .environment(appState)
}
.windowStyle(.hiddenTitleBar)
.defaultSize(width: 520, height: 560)
```

**Toolbar:**
- Left: Animated status dot (7×7, pulse 2s) + "Connected to Windows 11 Desktop"
- Right: "+ Add Device" button (blue bg, 28px high, rounded 6px)

**Device Card:**
```
┌──────────────────────────────────────────────┐
│ [Win]  Windows 11 Desktop         [2.1ms]   │
│  42×42   NearLink · DTLS 1.3       ⏻  🗑    │
│          Connected since 14:32               │
└──────────────────────────────────────────────┘
```
- Grid: 42px icon | 1fr content | auto actions
- Icon: 42×42, bg #e6f2ff, rounded 10px
- Background: rgba(255,255,255,0.88)
- Border: 1px solid rgba(109,122,140,0.18)
- Hover: shadow 0 2px 10px rgba(15,23,42,0.08)
- Latency badge: min-width 56px, rounded 999px, color-coded
- Action buttons: 26×26, transparent, hover #f1f5f9

**Collapsible Sections:**
- Header: 12px bold + chevron (rotates -90° on collapse)
- Border: 1px solid rgba(109,122,140,0.18)
- Background: rgba(255,255,255,0.74)

  **Key Mapping Section:** 3-column grid of key pairs
  - Key cap: min-width 34px, padding 5px 7px, border rgba(80,94,112,0.22), rounded 6px

  **Connection Mode Section:** 3 cards (Auto/NearLink/DirectLink)
  - Selected: border #007aff, bg #eff6ff
  - Badge: NearLink=#e8f7ff, DirectLink=#fef3c7, Auto=#e8fbf1

**Toast:** 底部弹出，bg rgba(30,41,59,0.92) + blur 10px, 2.2s 自动消失

**Navigation Bar:** 底部固定，bg #fafafa, border-top

### 3. Add Device Wizard (400×440)

```swift
Window("Add Device", id: "pairing") {
    PairingWizard()
        .environment(appState)
}
.windowStyle(.hiddenTitleBar)
.defaultSize(width: 400, height: 440)
```

**Step Indicator:** 3 circles (22×22, rounded 999px)
- Default: bg #e6ebf2, fg #6f7d91
- Active: bg #007aff, fg white
- Done: bg #10a36f, fg white

**Step 1 — Discovery:**
- Scanner: 110×90 container, 40×40 spinning ring
- Progress bar: 4px, blue fill 0→100% (3-5s)
- Device list: Mini cards (36×36 icon + info + signal badge)
  - Selected: bg #eff6ff, border-color blue
  - Signal badge: bg #e8f7ff, fg #0675b9
- Cancel + Connect buttons (32px high)

**Step 2 — Pairing:**
- "Pairing…" 状态, 步骤指示器前进

**Step 3 — Success:**
- Success icon: 56×56, green circle + checkmark
- scaleIn animation: 0.4s, scale 0→1
- Detail rows: Device/Protocol/Latency/Encryption
- Done button

### 4. Latency Monitor (420×480)

```swift
Window("Latency Monitor", id: "latency") {
    LatencyMonitorView()
        .environment(appState)
}
.defaultSize(width: 420, height: 480)
```

- **Hero:** 42px latency value, color-coded + unit "ms" 14px
- **Stats Summary:** 右上角, 11px muted
- **Bar Chart:** 24 根柱条, 高度 70px
  - Default: #bfdbfe, Recent 3: #60a5fa
  - Grid lines: 1ms/3ms/5ms 参考线 (dashed #e2e8f0)
  - Transition: 0.4s ease
- **Stats Grid:** 4 格 (Current/Min/Max/Jitter), bg #f8fafc
- **History Log:** 5 条记录, time + value + color
- **Alert Banner:** amber bg (#fef3c7), 延迟 >5ms 时 slideIn

### 5. Settings

```swift
Window("FlowGrid Settings", id: "settings") {
    SettingsView()
        .environment(appState)
}
```

标准 macOS TabView，分类：
- General: 启动选项、默认传输协议
- Network: NearLink WiFi 配置、DirectLink 端口
- Key Mapping: 全局映射开关、默认配置文件

## Core Layer Interfaces

### Transport Protocol

```swift
protocol TransportProtocol: Sendable {
    var transportType: TransportType { get }
    var state: ConnectionState { get async }
    func scan(duration: TimeInterval) async throws -> [DeviceInfo]
    func connect(to device: DeviceInfo) async throws -> AsyncStream<HIDFrame>
    func disconnect() async throws
    func sendHIDFrame(_ frame: HIDFrame) async throws
    func latencyStream() -> AsyncStream<LatencySample>
}
```

### DeviceManager (Actor)

```swift
actor DeviceManager: DeviceManaging {
    private var transports: [TransportType: TransportProtocol]
    private var connectedDevices: [String: ConnectedDevice] = [:]
    private let config: ConfigStore
    private let hal: PlatformHAL
    private let keyMapper: KeyMapper

    func startDiscovery() async throws -> [DeviceInfo]
    func connect(_ device: DeviceInfo, via transport: TransportType) async throws
    func disconnect(_ deviceId: String) async
    func setActiveDevice(_ deviceId: String) async
}
```

### NearLink Transport (主推)

```
发现 (BLE):
  CBCentralManager.scan → Nearby Device Service UUID
  → 读取设备名称、平台特征值
  → 获取 WiFi Direct 连接参数

连接:
  BLE 握手 → 交换 WiFi Direct 凭证
  → NWConnection (WiFi Direct)
  → DTLS 1.3 握手
  → 双向 HID 帧流

数据 (WiFi Direct):
  NWConnection 发送/接收 HIDFrame
  → 心跳保活 (2s)
  → 延迟测量 (ping-pong 时间戳)
```

### PlatformHAL (CGEvent)

```swift
final class macOSPlatformHAL: @unchecked Sendable {
    func injectKeyDown(_ key: CGKeyCode, modifiers: CGEventFlags)
    func injectKeyUp(_ key: CGKeyCode, modifiers: CGEventFlags)
    func injectMouseMoved(dx: Int32, dy: Int32)
    func injectMouseButtonDown(_ button: CGMouseButton)
    func injectMouseButtonUp(_ button: CGMouseButton)
    func checkAccessibilityPermission() -> Bool
    func currentFrontmostApp() -> String?
}
```

## Persistence

```swift
final class ConfigStore: @unchecked Sendable {
    // UserDefaults (简单设置)
    func loadSettings() -> AppSettings
    func saveSettings(_ settings: AppSettings)

    // 文件系统 (键位映射 YAML)
    func loadKeyMappingTable() throws -> MappingTable
    func saveKeyMappingTable(_ table: MappingTable) throws

    // 文件系统 (已知设备 JSON)
    func loadKnownDevices() -> [DeviceInfo]
    func saveKnownDevices(_ devices: [DeviceInfo]) throws

    // Paths
    // ~/Library/Application Support/FlowGrid/keymap.yaml
    // ~/Library/Application Support/FlowGrid/devices.json
}
```

## App Lifecycle

```swift
@main
struct FlowGridApp: App {
    @State private var appState: AppState

    init() {
        let config = ConfigStore()
        let nearLink = NearLinkTransport()
        let directLink = DirectLinkTransport()
        let manager = DeviceManager(
            transports: [.nearLink: nearLink, .directLink: directLink],
            config: config
        )
        _appState = State(initialValue: AppState(
            settings: config.loadSettings(),
            deviceManager: manager
        ))
    }

    var body: some Scene {
        MenuBarExtra { ... }
        Window("FlowGrid", id: "main") { ... }
        Window("Latency Monitor", id: "latency") { ... }
        Window("Settings", id: "settings") { ... }
        Window("Add Device", id: "pairing") { ... }
    }
}
```

```
启动:
  → 初始化 Core (ConfigStore, Transports, DeviceManager)
  → 创建 AppState (@Observable)
  → 显示菜单栏图标
  → 检查 Accessibility 权限
  → 如果 autoConnectOnLaunch: 连接上次设备

托盘:
  → 点击: 显示/隐藏托盘窗口
  → Show Connections (⌘1): 打开主窗口
  → Latency Monitor (⌘2): 打开延迟窗口
  → Preferences (⌘,): 打开设置
  → Quit (⌘Q): 断开设备 → 退出

设备连接:
  → "Add Device" → PairingWizard
  → 选择 NearLink (默认)
  → DeviceManager.startDiscovery()
  → 扫描 → 选择 → 配对 → 连接
  → 启动 HAL 事件循环 (键鼠转发)
```

## Animations

| 动画 | 时长 | SwiftUI 实现 |
|------|------|-------------|
| Status dot pulse | 2s infinite | `.animation(.easeInOut.repeatForever(), value:)` |
| Tray fadeIn | 0.25s | `.transition(.opacity.combined(with: .move(edge: .top)))` |
| Scanner spin | 1s linear | `.rotationEffect(.degrees(angle)).animation(.linear.repeatForever())` |
| Success scaleIn | 0.4s | `.scaleEffect().animation(.easeOut)` |
| Bar chart | 0.4s | `.animation(.easeOut(duration: 0.4), value: heights)` |
| Toast | 0.3s | `.transition(.move(edge: .bottom).combined(with: .opacity))` |
| Toggle | 0.2s | `.animation(.easeInOut(duration: 0.2), value:)` |
| Button hover | 0.15s | `.animation(.easeInOut(duration: 0.15), value: isHovered)` |
| Chevron rotate | 0.2s | `.rotationEffect().animation(.easeInOut)` |

## Entitlements

```xml
<!-- FlowGrid.entitlements -->
<key>com.apple.security.app-sandbox</key>
<false/>
<key>com.apple.developer.bluetooth</key>
<true/>
<!-- Accessibility permission via AXIsProcessTrusted() -->
```

## Info.plist

```xml
<key>LSUIElement</key>
<true/>  <!-- 纯菜单栏应用，不显示在 Dock -->
<key>LSMinimumSystemVersion</key>
<string>15.0</string>
```
