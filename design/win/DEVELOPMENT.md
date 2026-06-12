# FlowGrid Windows 11 Native Development Spec

## Overview

FlowGrid Windows 原生客户端，基于 WinUI 3 / Windows App SDK，采用 Fluent Design System 风格。支持 NearLink (BLE+WiFi) 和 DirectLink (TCP/mDNS) 双协议，作为系统托盘应用运行。

## Tech Stack

| 项 | 选择 |
|---|---|
| UI Framework | WinUI 3 (Windows App SDK 1.5+) |
| Language | C# / .NET 8 |
| BLE | Windows.Devices.Bluetooth.Advertisement |
| Network | System.Net.Sockets + mDNS (Bonjour/zeroconf) |
| HID Injection | Windows Input Simulator / Interception Driver |
| Tray | System.Windows.Forms.NotifyIcon 或 H.NotifyIcon |
| Config | JSON (System.Text.Json) + Registry |
| Target OS | Windows 10 1903+ / Windows 11 |

## Architecture

```
FlowGrid.sln
├── FlowGridCore/           — 传输层、HAL、键位映射 (类库)
│   ├── Transport/
│   │   ├── ITransport.cs
│   │   ├── NearLinkTransport.cs
│   │   └── DirectLinkTransport.cs
│   ├── HAL/
│   │   ├── PlatformHAL.cs
│   │   └── MouseInterpolator.cs
│   ├── KeyMapping/
│   │   ├── KeyMapper.cs
│   │   └── MappingTable.cs
│   ├── DeviceManager.cs
│   ├── LatencyMonitor.cs
│   └── ConfigStore.cs
├── FlowGridUI/             — WinUI 3 视图 (类库)
│   ├── Views/
│   │   ├── MainWindow.xaml
│   │   ├── TrayPopup.xaml
│   │   ├── AddDeviceWizard.xaml
│   │   ├── LatencyMonitor.xaml
│   │   ├── KeyMapperView.xaml
│   │   └── SettingsView.xaml
│   ├── ViewModels/
│   │   ├── MainViewModel.cs
│   │   ├── TrayViewModel.cs
│   │   ├── AddDeviceViewModel.cs
│   │   └── SettingsViewModel.cs
│   ├── Controls/
│   │   ├── DeviceCard.cs
│   │   ├── LatencyBadge.cs
│   │   ├── ToggleSwitch.cs
│   │   └── StepIndicator.cs
│   ├── Themes/
│   │   └── FlowGridTheme.xaml
│   └── Converters/
├── FlowGrid/               — 应用入口 (WinUI 3 App)
│   ├── App.xaml
│   ├── Program.cs
│   └── TrayIcon.cs
└── FlowGridTests/          — 单元测试
```

## Design System (Windows Fluent)

### Colors

```xml
<!-- FlowGridTheme.xaml -->
<Color x:Key="Ink">#171a1f</Color>
<Color x:Key="Muted">#68707d</Color>
<Color x:Key="Blue">#0878d8</Color>
<Color x:Key="Green">#10a36f</Color>
<Color x:Key="Red">#dc2626</Color>
<Color x:Key="Amber">#d97706</Color>
<Color x:Key="Line">#d9dee7</Color>
<Color x:Key="Paper">#f4f5f7</Color>

<!-- Latency States -->
<Color x:Key="LatencyGoodBg">#e8fbf1</Color>
<Color x:Key="LatencyGoodFg">#07966c</Color>
<Color x:Key="LatencyWarnBg">#fef3c7</Color>
<Color x:Key="LatencyWarnFg">#d97706</Color>
<Color x:Key="LatencyBadBg">#fee2e2</Color>
<Color x:Key="LatencyBadFg">#dc2626</Color>

<!-- Badge -->
<Color x:Key="BadgeBg">#e8f7ff</Color>
<Color x:Key="BadgeFg">#0675b9</Color>

<!-- Toggle -->
<Color x:Key="ToggleOn">#18a058</Color>
<Color x:Key="ToggleOff">#ccc</Color>
```

### Typography

```
Font Family: "Segoe UI", "Noto Sans SC", "Microsoft YaHei", sans-serif

Titlebar:     12px  Weight 700
Device Name:  13px  Weight 800
Meta Text:    11px  Weight 400  Color: Muted
Status:       12px  Weight 650
Section Head: 12px  Weight 800
Category:     11px  Weight 800  Uppercase  Letter-spacing 0.5px
Button:       12px  Weight 700
Badge:        10px  Weight 800
```

### Spacing & Sizing

```
Window Border Radius:   8px
Card Border Radius:     7px
Button Border Radius:   5px
Badge/Pill Radius:      999px
Icon Border Radius:     8px

Content Padding:        14px
Card Padding:           11px
Section Head Padding:   9px 10px
Toggle Size:            34×18px
Status Dot:             7×7px
```

### Shadows

```
Window:    0 18px 42px rgba(24,30,44,0.14)
Card:      0 16px 28px rgba(15,23,42,0.16)
Hover:     0 2px 10px rgba(15,23,42,0.08)
```

## Screen Specifications

### 1. Main Window (520×variable)

**Titlebar (34px):**
- Left: App icon (16×16, rounded 4px) + "FlowGrid"
- Center: "Connections"
- Right: Min/Max/Close (20×18 each)

**Toolbar:**
- Left: Status dot (7×7, pulse 2s) + "Connected to Windows 11 Desktop"
- Right: "+ Add Device" button (28px high, blue bg, rounded 5px)

**Device Card Grid (42px icon + 1fr + auto):**
```
┌─────────────────────────────────────────┐
│ [Win]  Windows 11 Desktop      [2.1ms] │
│  42×42   NearLink · Connected           │
│          ⏻ 🗑                           │
└─────────────────────────────────────────┘
```
- Border: 1px solid rgba(109,122,140,0.18)
- Background: rgba(255,255,255,0.88)
- Hover: box-shadow 0 2px 10px rgba(15,23,42,0.08)
- Disconnected: opacity 0.6

**Collapsible Sections:**
- Key Mapping: 3-column grid of key pairs (From → To)
- Network Latency: Bar chart (70px high, bars flex:1, #9cccf8, latest 3 highlighted)
- Clipboard History: List with icon+text, hover #f8fafc

**Navigation Bar (bottom):**
- Background: #fafafa, border-top: 1px solid #e2e8f0
- Buttons: 28px high, border 1px solid #d9dee7, rounded 6px

### 2. System Tray (280px width)

```
┌────────────────────────────────┐
│ FlowGrid          ● 2.4ms     │  Header
├────────────────────────────────┤
│ CONNECTION                     │
│ Windows 11 Desktop   NearLink │
│ Latency              2.4ms    │
│ + Add Device                  │
├────────────────────────────────┤
│ QUICK TOGGLES                 │
│ Key Mapping        [──● ON ]  │  34×18 toggle
│ Clipboard Sync     [──● ON ]  │
│ Mouse Smoothing    [●── OFF]  │
│ Auto Route         [──● ON ]  │
├────────────────────────────────┤
│ Open Main Window    Ctrl+⇧+F  │
│ Settings                       │
├────────────────────────────────┤
│ Quit FlowGrid                  │  Red (#dc2626)
└────────────────────────────────┘
```

- Background: rgba(255,255,255,0.95) with backdrop blur
- FadeIn animation: 0.2s ease, translateY -6px → 0
- Latency updates every 2000ms
- Status pill: bg #e8fbf1, fg #07966c, rounded 999px

### 3. Add Device Wizard (400px)

**Step Indicator:** 3 circles (22×22, rounded 999px)
- Default: bg #e6ebf2, fg #6f7d91
- Active: bg #0878d8, fg white
- Done: bg #10a36f, fg white

**Step 1 — Discovery:**
- Scanner animation: 110×90 container, 40×40 spinning ring
- Progress bar: 4px high, blue fill 0→100%
- Device list: Mini cards (36×36 icon + info + signal badge)
- Selected card: bg #eff6ff, border-color blue
- Buttons: Cancel (secondary) + Connect (primary, disabled until selected)

**Step 3 — Success:**
- Success icon: 56×56, green circle, checkmark, scaleIn 0.4s
- Detail rows: Device/Protocol/Latency/Encryption
- Done button

### 4. Settings (480px)

```
┌────────────────────────────────────┐
│ Connection Settings                │
├────────────────────────────────────┤
│ CONNECTION                         │
│ ┌──────────────────────────────┐   │
│ │ Auto select best route  [●] │   │  36×20 toggle
│ │ Preferred protocol  [NearLink]│  │  Clickable badge
│ │ Fallback timeout     ═══●══  │   │  Slider
│ └──────────────────────────────┘   │
│ SYNC                               │
│ ┌──────────────────────────────┐   │
│ │ Clipboard sync          [●] │   │
│ │ File transfer           [●] │   │
│ │ Notification mirroring  [○] │   │
│ └──────────────────────────────┘   │
│ INPUT                              │
│ ┌──────────────────────────────┐   │
│ │ Mouse smoothing         [○] │   │
│ │ Key repeat delay   ═══●══   │   │
│ └──────────────────────────────┘   │
│ SECURITY                           │
│ ┌──────────────────────────────┐   │
│ │ DTLS encryption         [●] │   │
│ │ Certificate pinning     [●] │   │
│ └──────────────────────────────┘   │
├────────────────────────────────────┤
│ ← Back   ● Unsaved   Save Cancel  │
└────────────────────────────────────┘
```

- Settings list: grid gap 1px, bg rgba(109,122,140,0.14) creates borders
- Row: min-height 46px, bg rgba(255,255,255,0.95)
- Category: 11px bold uppercase, letter-spacing 0.5px
- Unsaved indicator: amber (#d97706)

### 5. Latency Monitor

- Hero: 42px latency value + stats summary
- Bar chart: 24 bars, latest 3 highlighted (#60a5fa)
- Stats grid: 4 cards (Current/Min/Max/Jitter)
- History log: 5 most recent events
- Alert banner: amber bg (#fef3c7), slides in when >5ms

## Transport Implementation

### NearLink (BLE + WiFi Direct)

```csharp
// BLE Discovery
var watcher = new BluetoothLEAdvertisementWatcher();
watcher.AdvertisementFilter = new BluetoothLEAdvertisementFilter {
    Advertisement = new BluetoothLEAdvertisement {
        // FlowGrid Service UUID
    }
};

// WiFi Direct data connection
using var socket = new StreamSocket();
await socket.ConnectAsync(hostName, "FlowGrid-NearLink");
```

### DirectLink (TCP + mDNS)

```csharp
// mDNS Discovery
var service = new DnsServiceBrowser("_flowgrid._tcp");
var endpoints = await service.Browse();

// TCP Connection
using var client = new TcpClient();
await client.ConnectAsync(endpoint.Address, endpoint.Port);
```

## HID Injection (Windows)

```csharp
// Using Input Simulator
public class WindowsHAL : IPlatformHAL {
    private readonly InputSimulator _sim = new();

    public void InjectKeyDown(KeyCode key, ModifierKeys mods) {
        if (mods.HasFlag(ModifierKeys.Control)) _sim.Keyboard.KeyDown(VirtualKeyCode.CONTROL);
        if (mods.HasFlag(ModifierKeys.Shift)) _sim.Keyboard.KeyDown(VirtualKeyCode.SHIFT);
        _sim.Keyboard.KeyDown(ToVirtualKey(key));
    }

    public void InjectMouseMove(int dx, int dy) {
        _sim.Mouse.MoveMouseBy(dx, dy);
    }
}
```

## Config Persistence

```csharp
public class ConfigStore {
    private const string ConfigPath = "%APPDATA%/FlowGrid/config.json";
    private const string KeyMapPath = "%APPDATA%/FlowGrid/keymap.yaml";

    // Settings → JSON file
    public AppSettings LoadSettings() { ... }
    public void SaveSettings(AppSettings s) { ... }

    // Key mapping → YAML file
    public MappingTable LoadKeyMap() { ... }
    public void SaveKeyMap(MappingTable t) { ... }

    // Known devices → JSON file
    public List<DeviceInfo> LoadKnownDevices() { ... }
    public void SaveKnownDevices(List<DeviceInfo> d) { ... }
}
```

## App Lifecycle

```
App 启动:
  → 初始化 Core (ConfigStore, Transports, DeviceManager)
  → 创建托盘图标 (NotifyIcon)
  → 隐藏主窗口 (仅在托盘显示)
  → 注册全局热键 (Ctrl+Shift+F)
  → 如果 AutoConnect: 连接上次设备

托盘交互:
  → 左键点击: 显示/隐藏主窗口
  → 右键点击: 显示上下文菜单
  → 双击: 打开主窗口

退出:
  → 断开所有设备
  → 保存设置
  → 释放托盘图标
  → 退出
```

## Animations

| 动画 | 时长 | 类型 |
|------|------|------|
| Status dot pulse | 2s | infinite, opacity 1↔0.4 |
| Tray fadeIn | 0.2s | ease, translateY -6→0 |
| Scanner spin | 1s | linear infinite, rotate 360° |
| Success scaleIn | 0.4s | ease, scale 0→1 |
| Bar chart | 0.5s | ease, height transition |
| Toast | 0.3s | ease, translateY 80→0 |
| Toggle | 0.2s | ease |
| Button hover | 0.15s | brightness 1.1 |
| Button active | 0.15s | brightness 0.9, translateY 1px |

## File List

| 文件 | 说明 |
|------|------|
| FlowGridCore/ITransport.cs | 传输层接口 |
| FlowGridCore/NearLinkTransport.cs | NearLink 实现 |
| FlowGridCore/DirectLinkTransport.cs | DirectLink 实现 |
| FlowGridCore/PlatformHAL.cs | Windows HID 注入 |
| FlowGridCore/MouseInterpolator.cs | 鼠标插值 |
| FlowGridCore/KeyMapper.cs | 键位映射引擎 |
| FlowGridCore/DeviceManager.cs | 设备管理器 |
| FlowGridCore/LatencyMonitor.cs | 延迟监控 |
| FlowGridCore/ConfigStore.cs | 配置持久化 |
| FlowGridUI/MainWindow.xaml | 主窗口 |
| FlowGridUI/TrayPopup.xaml | 托盘弹出 |
| FlowGridUI/AddDeviceWizard.xaml | 配对向导 |
| FlowGridUI/LatencyMonitor.xaml | 延迟监控窗口 |
| FlowGridUI/KeyMapperView.xaml | 键位映射编辑器 |
| FlowGridUI/SettingsView.xaml | 设置窗口 |
| FlowGridUI/Themes/FlowGridTheme.xaml | 设计系统主题 |
| FlowGrid/App.xaml | 应用入口 |
| FlowGrid/Program.cs | 启动逻辑 |
| FlowGrid/TrayIcon.cs | 托盘图标管理 |
