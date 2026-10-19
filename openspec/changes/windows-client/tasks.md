## 1. OpenSpec

- [x] 1.1 本变更四件套（proposal / design / tasks / specs）

## 2. Go 核心进程（windows/）

- [x] 2.1 go.mod：module github.com/flowgrid/windows，replace protocol => ../protocol
- [x] 2.2 internal/core/core.go：设备表 + discoveryFeed 抽象（可测）+ mDNS 生命周期 + Prune + 变更广播
- [x] 2.3 internal/core/ipc.go：pipe accept 循环、信封编解码、hello/status/devices 分发、device.changed 广播
- [x] 2.4 cmd/fgcore/main.go：flags + slog + 装配 + 信号退出
- [x] 2.5 `go test ./...` 全绿 + `go vet ./...` 干净

## 3. WPF UI（windows/ui）

- [x] 3.1 FlowGrid.UI.csproj（net8.0-windows / UseWPF，零第三方包）
- [x] 3.2 CoreLink.cs：连接/退避重连/hello+devices+status 收发/事件回调
- [x] 3.3 MainWindow：连接状态徽标 + 设备列表
- [x] 3.4 `dotnet build` 零警告

## 4. 验证与仓库整备

- [x] 4.1 windows/smoke.ps1 管道端到端冒烟（hello → devices.list → status.get）
- [x] 4.2 根 .gitignore 增补 windows/ui bin/obj；AGENTS.md 目录地图与构建命令增补
- [x] 4.3 windows/README.md（结构、运行、冒烟说明）
