# FlowGrid KDE Plasma Native Development Spec

## Overview

FlowGrid KDE Plasma 原生客户端，基于 Kirigami / Qt 6 框架，采用 KDE Breeze Design 风格。支持 NearLink (BLE+WiFi) 和 DirectLink (TCP/mDNS) 双协议，作为系统托盘应用运行。

## Tech Stack

| 项 | 选择 |
|---|---|
| UI Framework | KDE Kirigami (Qt 6 QML) |
| Language | C++17 / Qt 6 QML |
| BLE | Qt Bluetooth (QBluetoothDeviceDiscoveryAgent) |
| Network | Qt Network (QTcpSocket + QZeroConf / KDNSSD) |
| HID Injection | uinput / evdev (Linux) |
| Tray | KStatusNotifierItem (KDE 系统托盘) |
| Config | KConfig (INI-style) + YAML (键位映射) |
| Target OS | KDE Plasma 5.27+ / Plasma 6 (Linux) |

## Architecture

```
flowgrid/
├── CMakeLists.txt                  — 顶层 CMake
├── src/
│   ├── core/                       — 核心层 (C++ 库)
│   │   ├── Transport/
│   │   │   ├── Transport.h/cpp             — 传输层接口
│   │   │   ├── NearLinkTransport.h/cpp     — NearLink 实现
│   │   │   └── DirectLinkTransport.h/cpp   — DirectLink 实现
│   │   ├── HAL/
│   │   │   ├── LinuxHAL.h/cpp              — uinput/evdev HID 注入
│   │   │   └── MouseInterpolator.h/cpp     — 鼠标插值
│   │   ├── KeyMapping/
│   │   │   ├── KeyMapper.h/cpp             — 键位映射引擎
│   │   │   └── MappingTable.h/cpp          — 映射表管理
│   │   ├── DeviceManager.h/cpp             — 设备管理器
│   │   ├── LatencyMonitor.h/cpp            — 延迟监控
│   │   └── ConfigStore.h/cpp               — KConfig 持久化
│   ├── ui/                         — Kirigami QML 视图
│   │   ├── Main.qml
│   │   ├── TrayPopup.qml
│   │   ├── AddDeviceWizard.qml
│   │   ├── LatencyMonitor.qml
│   │   ├── KeyMapperView.qml
│   │   ├── SettingsView.qml
│   │   └── components/
│   │       ├── DeviceCard.qml
│   │       ├── LatencyBadge.qml
│   │       ├── StatusDot.qml
│   │       └── StepIndicator.qml
│   ├── models/                     — Qt 数据模型
│   │   ├── DeviceListModel.h/cpp
│   │   ├── LatencyModel.h/cpp
│   │   └── KeyMapModel.h/cpp
│   ├── backend.h/cpp               — QML 后端单例
│   └── main.cpp                    — 应用入口
├── resources/
│   ├── icons/                      — SVG 图标 (Breeze 风格)
│   ├── qml/                        — QML 资源
│   └── org.flowgrid.desktop        — .desktop 文件
└── tests/                          — 单元测试
```

## Design System (KDE Breeze)

### Colors

```qml
// Qt/QML Color Constants
readonly property color ink:      "#171a1f"
readonly property color muted:    "#68707d"
readonly property color kdeBlue:  "#3daee9"
readonly property color green:    "#10a36f"
readonly property color red:      "#dc2626"
readonly property color amber:    "#d97706"
readonly property color line:     "#d9dee7"

// KDE Titlebar
readonly property color titlebarBg:    "#31363b"  // Breeze Dark
readonly property color titlebarFg:    "#eef1f4"

// Latency States
readonly property color latencyGoodBg: "#e8fbf1"
readonly property color latencyGoodFg: "#07966c"
readonly property color latencyWarnBg: "#fef3c7"
readonly property color latencyWarnFg: "#d97706"
readonly property color latencyBadBg:  "#fee2e2"
readonly property color latencyBadFg:  "#dc2626"

// Badge
readonly property color badgeBg: "#e8f7ff"
readonly property color badgeFg: "#0675b9"

// Toggle
readonly property color toggleOn:  "#18a058"
readonly property color toggleOff: "#ccc"

// Context Badges (Key Mapper)
readonly property color ctxGlobalBg: "#e8f7ff"   fg: "#0675b9"
readonly property color ctxAppsBg:   "#fff7e6"   fg: "#d48806"
readonly property color ctxGameBg:   "#f3e8ff"   fg: "#7c3aed"
```

### Typography

```
Font Family: "Noto Sans", "Noto Sans SC", "Segoe UI", sans-serif

Titlebar:     12px  Weight 700
Window Title: 22px  Weight 700
Device Name:  13px  Weight 800
Meta Text:    11px  Weight 400  Color: muted
Section Head: 12px  Weight 800
Category:     11px  Weight 700  Uppercase
Button:       12px  Weight 700
Badge:        10px  Weight 800
Key Cap:      11px  Weight 800
Table Header: 10px  Weight 700  Uppercase
```

### Spacing & Sizing

```
Window Border Radius:   6px   (KDE style, smaller than Mac/Win)
Card Border Radius:     7px
Button Border Radius:   3px
Badge/Pill Radius:      999px
Icon Border Radius:     4px or 8px

Content Padding:        14px
Card Padding:           11px
Section Head Padding:   9px 10px
Toggle Size:            34×18px
Status Dot:             7×7px
Key Cap:                min-width 34px, padding 3px 8px
Step Circle:            22×22px (simplified: 18×18px)
```

### Shadows

```
Window:    0 18px 42px rgba(24,30,44,0.14)
Card:      0 16px 28px rgba(15,23,42,0.16)
Hover:     0 2px 10px rgba(15,23,42,0.08)
```

## Screen Specifications

### 1. Main Window (480-520px width)

**Titlebar (30px):**
- Background: #31363b (Breeze Dark)
- Left: App icon (16×16, rounded 4px, KDE blue) + "FlowGrid"
- Center: "Connections"
- Right: Minimize/Maximize/Close buttons
- Text color: #eef1f4

**Toolbar:**
- Left: Status dot (7×7, pulse 2s, green) + "Connected to Windows 11 Desktop"
- Right: "+ Add Device" button (28px high, KDE blue bg, rounded 3px)

**Device Card:**
```
┌─────────────────────────────────────────┐
│ [Win]  Windows 11 Desktop      [2.1ms] │
│  42×42   NearLink · Connected    ⏻ 🗑   │
└─────────────────────────────────────────┘
```
- Background gradient: #fbfcfd → #eff2f6
- Border: 1px solid rgba(109,122,140,0.18)
- Hover: box-shadow 0 2px 10px rgba(15,23,42,0.08)
- Disconnected: opacity 0.55

**Collapsible Sections (same pattern as Windows):**
- Section head: padding 9px 10px, chevron rotates on collapse
- Body: transition 0.2s ease

### 2. System Tray (260-280px width)

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
│ Quit FlowGrid                  │  Red
└────────────────────────────────┘
```

- Background: rgba(247,249,251,0.95) with backdrop blur
- Separator: 1px, rgba(110,123,141,0.12)
- Hover: #f1f5f9, Active: #e2e8f0

### 3. Key Mapping Editor (520-580px width)

**Toolbar:**
- Search input
- Filter pills: All / Global / Apps / Game (26px high, rounded 999px)
  - Active: KDE blue bg, white text
  - Inactive: white bg, muted text, border #d9dee7

**Table:**
- Sticky header: 10px uppercase, #5d6878
- Row hover: #f8fafc
- Border: rgba(100,116,139,0.1-0.16)
- Font: 11px

**Key Caps:**
- Min-width 34px, padding 3px 8px
- Border: 1px solid rgba(80,94,112,0.2)
- Shadow: 0 1px 2px rgba(0,0,0,0.04)
- Border radius: 6px

**Context Badges:**
- Global: bg #e8f7ff, fg #0675b9
- Apps: bg #fff7e6, fg #d48806
- Game: bg #f3e8ff, fg #7c3aed

### 4. Settings (420-480px width)

**Same layout as Windows but with KDE styling:**
- Settings list: grid gap 1px, bg rgba(109,122,140,0.14)
- Row: min-height 42px, bg rgba(255,255,255,0.9)
- Toggle: 34-36×18-20px
- Slider: 4px track, KDE blue thumb (14px)
- Protocol badge: clickable, cycles NearLink → DirectLink → Auto

**Navigation Bar (bottom):**
- Background: #fafafa, border-top: #d9dee7
- Buttons: 28px high, rounded 3px
- Primary button: KDE blue
- Unsaved indicator: amber

### 5. Add Device Wizard (400px)

Same three-step flow as Windows/Mac:
- Step circles: 22×22px
- Scanner: spinning ring animation
- Device list: mini cards with selection highlight
- Success: green circle + checkmark + detail rows

## Transport Implementation

### NearLink (BLE + WiFi Direct)

```cpp
// BLE Discovery (Qt Bluetooth)
class NearLinkTransport : public Transport {
    QBluetoothDeviceDiscoveryAgent *m_agent;

    void startScan() {
        m_agent = new QBluetoothDeviceDiscoveryAgent(this);
        connect(m_agent, &QBluetoothDeviceDiscoveryAgent::deviceDiscovered,
                this, &NearLinkTransport::onDeviceDiscovered);
        m_agent->start(QBluetoothDeviceDiscoveryAgent::LowEnergyMethod);
    }
};

// WiFi Direct data connection
class WiFiDirectSocket : public QObject {
    QTcpSocket *m_socket;

    void connectToHost(const QHostAddress &addr, quint16 port) {
        m_socket = new QTcpSocket(this);
        m_socket->connectToHost(addr, port);
        // DTLS handshake via QSslSocket
    }
};
```

### DirectLink (TCP + mDNS)

```cpp
// mDNS Discovery (KDNSSD / Avahi)
class DirectLinkTransport : public Transport {
    KDNSSD::ServiceBrowser *m_browser;

    void startScan() {
        m_browser = new KDNSSD::ServiceBrowser("_flowgrid._tcp");
        connect(m_browser, &KDNSSD::ServiceBrowser::serviceAdded,
                this, &DirectLinkTransport::onServiceFound);
        m_browser->startBrowse();
    }
};
```

## HID Injection (Linux)

```cpp
// Using uinput for keyboard/mouse injection
class LinuxHAL : public PlatformHAL {
    int m_uinputFd;

    bool init() {
        m_uinputFd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
        // Setup EV_KEY, EV_REL events
        ioctl(m_uinputFd, UI_SET_EVBIT, EV_KEY);
        ioctl(m_uinputFd, UI_SET_EVBIT, EV_REL);
        // ... register all key codes
        // Create uinput device
        struct uinput_user_dev udev = {};
        strcpy(udev.name, "FlowGrid Virtual Input");
        udev.id.bustype = BUS_USB;
        write(m_uinputFd, &udev, sizeof(udev));
        ioctl(m_uinputFd, UI_DEV_CREATE);
        return true;
    }

    void injectKeyDown(int keyCode) override {
        struct input_event ev = {
            .type = EV_KEY,
            .code = keyCode,
            .value = 1
        };
        write(m_uinputFd, &ev, sizeof(ev));
        // SYN event
        ev.type = EV_SYN;
        ev.code = SYN_REPORT;
        ev.value = 0;
        write(m_uinputFd, &ev, sizeof(ev));
    }

    void injectMouseMove(int dx, int dy) override {
        struct input_event ev = {
            .type = EV_REL,
            .code = REL_X,
            .value = dx
        };
        write(m_uinputFd, &ev, sizeof(ev));
        ev.code = REL_Y;
        ev.value = dy;
        write(m_uinputFd, &ev, sizeof(ev));
        // SYN
    }
};
```

## Config Persistence

```cpp
class ConfigStore {
    KConfig m_config{"flowgridrc"};
    static const QString KeyMapPath;  // ~/.config/flowgrid/keymap.yaml

    AppSettings loadSettings() {
        KConfigGroup grp = m_config.group("General");
        return AppSettings{
            .preferredTransport = grp.readEntry("Transport", "nearLink"),
            .keyMappingEnabled = grp.readEntry("KeyMapping", true),
            .clipboardSyncEnabled = grp.readEntry("ClipboardSync", true),
            .mouseSmoothingEnabled = grp.readEntry("MouseSmoothing", false),
            .autoConnectOnLaunch = grp.readEntry("AutoConnect", true),
        };
    }

    void saveSettings(const AppSettings &s) {
        KConfigGroup grp = m_config.group("General");
        grp.writeEntry("Transport", s.preferredTransport);
        grp.writeEntry("KeyMapping", s.keyMappingEnabled);
        // ...
        grp.sync();
    }

    // Key mapping: YAML file at ~/.config/flowgrid/keymap.yaml
    // Known devices: JSON at ~/.config/flowgrid/devices.json
};
```

## System Tray (KDE)

```cpp
// Using KStatusNotifierItem for KDE system tray
class TrayIcon : public QObject {
    KStatusNotifierItem *m_tray;

    void init() {
        m_tray = new KStatusNotifierItem(this);
        m_tray->setCategory(KStatusNotifierItem::ApplicationStatus);
        m_tray->setStatus(KStatusNotifierItem::Active);
        m_tray->setTitle("FlowGrid");
        m_tray->setIconByName("flowgrid");

        // Context menu
        QMenu *menu = m_tray->contextMenu();
        menu->addAction("Show Connections", this, &TrayIcon::showMainWindow);
        menu->addAction("Latency Monitor", this, &TrayIcon::showLatencyMonitor);
        menu->addSeparator();
        menu->addAction("Settings", this, &TrayIcon::showSettings);
        menu->addSeparator();
        menu->addAction("Quit", qApp, &QApplication::quit);
    }
};
```

## App Lifecycle

```
App 启动:
  → 初始化 Core (ConfigStore, Transports, DeviceManager)
  → 创建 KStatusNotifierItem (系统托盘)
  → 注册 D-Bus 服务 (org.flowgrid)
  → 检查 uinput 权限
  → 如果 AutoConnect: 连接上次设备

托盘交互:
  → 左键/中键: 显示/隐藏主窗口
  → 滚轮: 切换活跃设备
  → 右键: 上下文菜单

D-Bus 集成:
  → org.flowgrid 服务注册
  → /org/flowgrid 对象路径
  → 方法: Connect/Disconnect/ListDevices
  → 信号: DeviceConnected/DeviceDisconnected/LatencyUpdate

退出:
  → 断开所有设备
  → 保存设置
  → 释放 uinput 设备
  → 注销 D-Bus
  → 退出
```

## CMake Build

```cmake
cmake_minimum_required(VERSION 3.20)
project(flowgrid VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Core Quick Network Bluetooth)
find_package(KF6 REQUIRED COMPONENTS Kirigami Config CoreAddons WindowSystem)
find_package(KF6 REQUIRED COMPONENTS StatusNotifierItem)

# Core library
add_library(flowgrid_core STATIC
    src/core/Transport/Transport.cpp
    src/core/Transport/NearLinkTransport.cpp
    src/core/Transport/DirectLinkTransport.cpp
    src/core/HAL/LinuxHAL.cpp
    src/core/HAL/MouseInterpolator.cpp
    src/core/KeyMapping/KeyMapper.cpp
    src/core/DeviceManager.cpp
    src/core/LatencyMonitor.cpp
    src/core/ConfigStore.cpp
)

target_link_libraries(flowgrid_core
    Qt6::Core Qt6::Network Qt6::Bluetooth
    KF6::ConfigCore
)

# Application
add_executable(flowgrid
    src/main.cpp src/backend.cpp
    src/models/DeviceListModel.cpp
    src/models/LatencyModel.cpp
    src/models/KeyMapModel.cpp
)

target_link_libraries(flowgrid
    flowgrid_core
    Qt6::Quick Qt6::Network Qt6::Bluetooth
    KF6::Kirigami KF6::StatusNotifierItem
)
```

## Animations

| 动画 | 时长 | 类型 | QML 实现 |
|------|------|------|----------|
| Status dot pulse | 2s | infinite | SequentialAnimation on opacity |
| Tray fadeIn | 0.2s | ease | NumberAnimation on y + opacity |
| Scanner spin | 1s | linear infinite | RotationAnimation |
| Success scaleIn | 0.4s | ease | SpringAnimation on scale |
| Bar chart | 0.4s | ease | Behavior on height |
| Toast | 0.3s | ease | NumberAnimation |
| Toggle | 0.2s | ease | Behavior on x |
| Button active | 0.15s | ease | ScaleAnimator + ColorAnimation |

## File List

| 文件 | 说明 |
|------|------|
| src/core/Transport.h | 传输层接口 |
| src/core/NearLinkTransport.h/cpp | NearLink 实现 |
| src/core/DirectLinkTransport.h/cpp | DirectLink 实现 |
| src/core/LinuxHAL.h/cpp | Linux uinput HID 注入 |
| src/core/MouseInterpolator.h/cpp | 鼠标插值 |
| src/core/KeyMapper.h/cpp | 键位映射引擎 |
| src/core/MappingTable.h/cpp | 映射表管理 |
| src/core/DeviceManager.h/cpp | 设备管理器 |
| src/core/LatencyMonitor.h/cpp | 延迟监控 |
| src/core/ConfigStore.h/cpp | KConfig 持久化 |
| src/ui/Main.qml | 主窗口 |
| src/ui/TrayPopup.qml | 托盘弹出内容 |
| src/ui/AddDeviceWizard.qml | 配对向导 |
| src/ui/LatencyMonitor.qml | 延迟监控窗口 |
| src/ui/KeyMapperView.qml | 键位映射编辑器 |
| src/ui/SettingsView.qml | 设置窗口 |
| src/ui/components/DeviceCard.qml | 设备卡片组件 |
| src/ui/components/LatencyBadge.qml | 延迟徽章 |
| src/ui/components/StatusDot.qml | 状态指示灯 |
| src/ui/components/StepIndicator.qml | 步骤指示器 |
| src/models/DeviceListModel.h/cpp | 设备列表模型 |
| src/models/LatencyModel.h/cpp | 延迟数据模型 |
| src/models/KeyMapModel.h/cpp | 键位映射模型 |
| src/backend.h/cpp | QML 后端桥接 |
| src/main.cpp | 应用入口 |
| resources/org.flowgrid.desktop | .desktop 文件 |
| resources/icons/flowgrid.svg | 应用图标 |
