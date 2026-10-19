# Windows 客户端：进程骨架与 IPC 契约

## Why

Windows 客户端立项。技术栈已经本机盘点 + 实测定案：**.NET 8 WPF 做 UI，协议栈留在 Go**（本机无 gcc，cgo/FFI 面不可用；.NET 8 SDK + WindowsDesktop 运行时零配置可构建，已实测）。动手写代码前，先把进程边界与 UI↔核心的 IPC 契约定下来，避免 UI 与协议层长死在一起。

架构上的硬约束（与 UI 栈无关的 Windows 规则）：
- 覆盖 UAC/安全桌面的键鼠注入必须由提权进程完成；服务进程在 session 0 且禁止承载 UI → 「系统层核心 + 用户会话 UI」分进程是终态结构（Synergy/Barrier 同构，deepin 端「必须系统级」是同一原因）
- 剪贴板监听（AddClipboardFormatListener）需要交互会话里的消息窗口 → 落在 UI 进程

## What Changes

- **新增 `windows/` 目录**：
  - Go 核心进程 `fgcore`（module `github.com/flowgrid/windows`，直接消费 `github.com/flowgrid/protocol`）。MVP 范围：mDNS 发现桥接 + 设备表 + 命名管道 IPC 服务端
  - WPF UI `FlowGrid.UI`（net8.0-windows）。MVP 范围：管道客户端（自动重连）+ 核心连接状态 + 设备列表
- **新增 IPC 契约**（客户端内部通道，非 wire format）：命名管道 `\\.\pipe\flowgrid-core`，JSON Lines 信封 `{v,type,id,payload}`；首批消息 hello / status / devices 三组 + `device.changed` 推送
- **Non-Goals（后续变更）**：输入注入与捕获、对端连接（DTLS/握手）、剪贴板/文件传输接线、SYSTEM 服务（fgcored）、设计系统皮肤化

## Capabilities

### New Capabilities
- `windows-ipc`: UI↔核心命名管道信封协议、首批消息集、UI 重连语义
- `windows-core`: fgcore 生命周期、mDNS 发现桥接、设备表与变更广播

## Impact

- 新增 `windows/`（Go module + WPF 工程）与 `windows/README.md`；根 `.gitignore` 增补 .NET 输出
- `protocol/` 零改动（纯消费方）；`design/PROTOCOL.md` 不涉及（IPC 不是 wire format）
- 验证：`cd windows && go test ./... && go vet ./...`；`dotnet build windows/ui`；PowerShell 管道客户端对 fgcore 端到端冒烟
