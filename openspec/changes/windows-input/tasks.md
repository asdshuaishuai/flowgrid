## 1. OpenSpec

- [x] 1.1 本变更四件套

## 2. 平台面（internal/wininput）

- [x] 2.1 HID↔VK 静态映射表（§6.1 全集 + 修饰键左右区分）
- [x] 2.2 SendInput 注入（键/鼠标移动/按键/滚轮）
- [x] 2.3 低级钩子捕获（WH_KEYBOARD_LL/WH_MOUSE_LL + 消息泵线程 + 事件队列）
- [x] 2.4 前台应用检测 + 剪贴板消息窗（WM_CLIPBOARDUPDATE，CF_UNICODETEXT）

## 3. 路由与会话（internal/link + internal/hub）

- [x] 3.1 link.session 挂 ClipboardSession/FileSender/FileReceiver + 帧就地分发 + Pump ticker
- [x] 3.2 Engine：OnPeerFrame/SendToPeer/BroadcastToPeers/OnlinePeerNames + 剪贴板/文件回调 + 延迟统计入快照
- [x] 3.3 hub：注入入站 HID 帧；捕获转发；切换热键 Ctrl+Alt+0..8；键位映射（YAML + RemapModifiers）
- [x] 3.4 剪贴板双向（广播 + 回声抑制）；文件自动接受落 downloads

## 4. IPC 与 UI

- [x] 4.1 IPC：input.status/input.changed、switch.request、clipboard.set/text、file.send/transfers/progress、peers 延迟统计
- [x] 4.2 UI：文件屏真实收发（选文件/选对端/进度）、延迟屏真实数据、键位屏状态、连接屏托管指示

## 5. 验证

- [x] 5.1 单测：HID↔VK 映射、键位重映射、引擎级文件环回落盘、剪贴板环回重组
- [x] 5.2 smoke-link 扩展：剪贴板 A→B 端到端（clipboard.text 读回断言）+ 文件 A→B 落盘比对
- [x] 5.3 全部测试绿 + dotnet build 零警告 + 协议层零改动核查
