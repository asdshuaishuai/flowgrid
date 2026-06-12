# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

FlowGrid is a cross-platform keyboard and mouse sharing application (similar to Synergy/Deskflow/Logitech Flow) that lets one keyboard+mouse control multiple computers. Currently in the **design phase** — no implementation code exists yet, only protocol specs, architecture docs, and HTML UI mockups under `design/`.

**Documentation language:** Most docs are written in Chinese with English technical terms.

## Repository Structure

```
design/
├── PROTOCOL.md              — Canonical protocol specification (v1.0), the source of truth for all wire formats
├── D2link.html              — Product design bluebook: architecture, competitive analysis, 5-layer model
├── index.html               — Design system landing page (colors, typography, spacing tokens)
├── mac/DEVELOPMENT.md       — macOS implementation spec (Swift 6 + SwiftUI)
├── win/DEVELOPMENT.md       — Windows implementation spec (C# / .NET 8 + WinUI 3)
├── kde/DEVELOPMENT.md       — Linux/KDE implementation spec (C++17 + Qt 6 / Kirigami)
├── mac/*.html, win/*.html, kde/*.html — UI mockups per platform
```

## Dual Protocol Architecture

FlowGrid uses two protocols sharing the same HID frame format and keycode system:

- **NearLink** (primary): BLE 5.0 discovery → WiFi Direct P2P → UDP + DTLS 1.3. Target <3ms latency. No router needed.
- **DirectLink** (fallback): mDNS discovery → Ethernet/WiFi LAN → TCP + UDP. Target <0.5ms. For wired/physically-secure environments.

Design philosophy: AirDrop-inspired — BLE for discovery/handshake, high-speed channel for data. UDP+DTLS chosen over TCP because 8-byte HID frames trigger Nagle's 40-120ms delay accumulation.

## 5-Layer Architecture

All platform implementations follow this layered design:

1. **L1 — UI Shell**: System tray resident app, pairing wizard, latency display, key mapping editor
2. **L2 — KeyMapper**: OS-aware key remapping (detects OS on both ends, loads rule tables, intercepts HID scancodes, remaps and injects). YAML hot-reload for app-context rules.
3. **L3 — Transport**: NearLink or DirectLink. BLE discovery, WiFi Direct, DTLS encryption, UDP HID frame sender (8 bytes/frame), auto-reconnect (<200ms).
4. **L4 — Platform HAL**: Unified HID event format with platform-specific injection: Windows (SendInput/Raw Input), macOS (IOKit HID + CGEventTap), Linux (libei/uinput/libportal).
5. **L5 — Hardware/Network**: WiFi, BLE 5.0, Ethernet.

**Core data flow:** Host keyboard/mouse → PlatformHAL capture → KeyMapper remap → HIDFrame serialization → Transport (UDP/TCP) → Target PlatformHAL injection.

## Key Abstractions (shared across platforms)

| Abstraction | Role |
|---|---|
| `TransportProtocol` / `ITransport` | Interface with `NearLinkTransport` and `DirectLinkTransport` implementations |
| `PlatformHAL` | Platform-specific HID capture and injection |
| `DeviceManager` | Manages transport lifecycle and connected devices (Actor/singleton) |
| `KeyMapper` | OS-aware key remapping engine (Actor) with YAML rule files |
| `LatencyMonitor` | Ping/pong latency tracking |
| `ConfigStore` | Persistence (platform-specific: UserDefaults + YAML / JSON + Registry / KConfig + YAML) |
| `MouseInterpolator` | Smooths high-DPI mouse movements on Host side |

## Per-Platform Implementation Notes

### macOS (Swift 6 + SwiftUI)
- **Build:** XcodeGen (`project.yml`) + Swift Package Manager
- **Structure:** Two SPM packages — `FlowGridCore` (transport/HAL/keymapping) and `FlowGridUI` (SwiftUI views), plus `FlowGrid` app target for assembly
- **App type:** Pure menu bar app using `MenuBarExtra(.window style)`
- **Concurrency:** Swift 6 strict concurrency; `DeviceManager` and `KeyMapper` are actors
- **DI:** `DependencyContainer.swift` in the app target assembles core components
- **Target:** macOS 15+ (Sequoia)

### Windows (C# / .NET 8 + WinUI 3)
- **Build:** .NET 8 solution (`FlowGrid.sln`)
- **Structure:** `FlowGridCore` (class library), `FlowGridUI` (WinUI 3 views + ViewModels), `FlowGrid` (app entry)
- **DI:** Constructor injection via ViewModels
- **Config:** JSON (`System.Text.Json`) + Registry
- **Target:** Windows 10 1903+ / Windows 11, Windows App SDK 1.5+

### Linux/KDE (C++17 + Qt 6 / Kirigami)
- **Build:** CMake 3.20+, requires Qt 6 (Core, Quick, Network, Bluetooth) and KDE Frameworks 6 (Kirigami, Config, StatusNotifierItem)
- **Structure:** `src/core/` (C++ library), `src/ui/` (QML views), `src/models/` (Qt data models), `src/backend.h/cpp` (QML bridge singleton)
- **Tray:** KStatusNotifierItem
- **Config:** KConfig (INI) + YAML for keymaps
- **Target:** KDE Plasma 5.27+ / Plasma 6

## Protocol Implementation Rules

- All protocol fields use **big-endian (Network Byte Order)** unless otherwise specified
- Structs are **not padded** — tightly packed
- HID frame size: **8 bytes** fixed
- Reserved fields **must be zeroed** on send, **ignored** on receive
- BLE Service UUID: `00001850-0000-1000-8000-00805F9B34FB` (0x1850)
- Protocol version: 1, Frame version: 1, Key Map version: "1.0"

When implementing, refer to `design/PROTOCOL.md` as the canonical reference for all wire formats, discovery packets, handshake sequences, HID frame layouts, keycode tables, and error codes.
