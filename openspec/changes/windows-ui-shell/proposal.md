# Windows UI 壳与设计令牌

## Why

windows-client 骨架已通（IPC + mDNS + 状态/设备列表），但界面还是功能原型样式。design/unified 是未来客户端 GUI 的唯一设计来源（AGENTS.md §九），deepin 端已有 designtokens.h 的移植先例。本变更把 WPF UI 对齐设计系统：令牌 1:1 移植 + 应用壳 + 导航 + 连接屏真实落地，其余屏按设计落空态骨架。UI 层是纯客户端工作，协议层零改动（发现协议问题记 `docs/PROTOCOL-ISSUES.md`）。

## What Changes

- **设计令牌**：`design/unified/tokens.css` v2 的 L2 语义层（light/dark 双值）+ L3 Windows 强调色（`#0878d8`/`#4a9eff`）移植为 WPF ResourceDictionary；几何/排版/圆角/间距数值与 designtokens.h 同源（§0–§5）
- **应用壳**：520px 面板 + 34px 自绘标题栏（拖拽/关闭）+ 左侧 44px 导航轨（8 目的地）。设计稿未定义壳层导航（各屏独立面板），导航轨是 WPF 壳层适配决策
- **连接屏**：核心状态 pill + 设备卡列表（live，来自 fgcore mDNS）+ 空态 + 键位映射/网络延迟折叠区骨架
- **其余 6 屏**：剪贴板/文件/延迟/键位映射/配对/设置——按设计落地页骨架与空态，功能接线属后续变更（依赖对端连接/传输里程碑）
- **设置持久化**：主题（跟随系统/浅色/深色）存 `%APPDATA%\FlowGrid\ui.json`，运行时切换不重启
- **托盘**：NotifyIcon + 原生菜单（打开/退出）；设计稿的 300px 托盘浮板为后续变更
- **Non-Goals**：对端连接（DTLS/握手）、输入注入、剪贴板/文件传输接线、托盘浮板、fgcored、IPC 消息集扩充

## Capabilities

### New Capabilities
- `windows-ui-shell`: 设计令牌消费规则、壳/导航/主题、连接屏、托盘

## Impact

- `windows/ui/`：新增 Themes（令牌/主题字典）、Views（8 屏）、ShellWindow、ThemeManager、TrayIcon；MainWindow.xaml 由单页改造成壳
- FlowGrid.UI.csproj 增 UseWindowsForms（NotifyIcon 互操作，WindowsDesktop 运行时自带）
- 协议层零改动；验证 = dotnet build 零警告 + 既有 go test 仍绿 + 协议目录 diff 为空
