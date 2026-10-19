# Windows 输入/剪贴板/文件全量接线

## Why

连接已通但对端帧只是被忽略——FlowGrid 的产品价值（跨机键鼠、剪贴板、文件）尚未接线。协议库已备齐全部会话（ClipboardSession / FileSender / FileReceiver / KeyMapTable / RemapModifiers / HID 常量），本变更把这些会话接到 Windows 平台面（SendInput 注入、低级钩子捕获、剪贴板监听、前台应用）上，客户端成为功能完整的 FlowGrid 节点。协议层零改动。

## What Changes

- **新增 `internal/wininput`**：HID Usage ↔ VK 映射、SendInput 注入（键/鼠标/滚轮）、低级钩子捕获（WH_KEYBOARD_LL/WH_MOUSE_LL + 专用消息泵线程）、前台应用检测
- **新增 `internal/hub`**：输入路由中枢——注入入站 HID 帧；捕获转发到「正在控制」的对端；切换热键 Ctrl+Alt+0..8（0=本机，1..8=在线对端按实例名序）；托管期间本机输入被钩子吞掉（切换热键除外）；键位映射（KeyMapTable YAML + RemapModifiers 跨平台修饰键语义 §6.3）
- **剪贴板**：fgcore 建消息窗监听 WM_CLIPBOARDUPDATE（CF_UNICODETEXT）→ ClipboardSession 发送到全部在线对端；入站重组完成 → 写本地剪贴板（回声抑制）；IPC `clipboard.set/get`
- **文件**：每链接 FileSender/FileReceiver；入站 OFFER 自动接受到 `-data/downloads`（≤16MiB，SHA-256 校验后落盘）；IPC `file.send/file.transfers` + `file.progress` 推送；UI 文件屏真实收发
- **延迟/键位屏真实数据**：LinkSnapshot 增 min/max/avg 延迟；keymap 状态（加载路径/规则数）
- **IPC 扩展**：`input.status/input.changed`、`switch.request`、`clipboard.set/clipboard.text`、`file.send/file.transfers/file.progress`、peers 快照增延迟统计
- **Non-Goals**：屏幕边缘切换、光标加速曲线（附录 C）、非文本剪贴板（图片/文件引用）、YAML 热重载（加载一次）、多显示器感知

## Capabilities

### New Capabilities
- `windows-input`: HID 注入/捕获、切换热键、键位映射应用
- `windows-files`: 文件收发与下载目录、IPC 进度语义

## Impact

- `windows/`：internal/wininput、internal/hub（新包）；internal/link 增帧分发与剪贴板/文件会话；IPC/UI 扩展
- 协议层零改动；验证 = 引擎级环回测试（文件落盘/剪贴板重组）+ smoke-link 扩展（剪贴板端到端读回 + 文件落盘比对）+ 全部既有测试仍绿
