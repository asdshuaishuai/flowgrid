## Context

协议库提供：ClipboardSession（分段/重组/超时/去重）、FileSender（OFFER/ACK/Pump/进度/SHA-256）、FileReceiver（OnOffer→Accept→校验后 OnComplete）、KeyMapTable.Lookup + RemapModifiers（§6.3 跨平台修饰键）、HIDKey*/Mod* 常量、全部载荷编解码。平台面：SendInput / SetWindowsHookEx(WH_*_LL) / AddClipboardFormatListener / GetForegroundWindow（§13.2 Windows 要求）。连接引擎（windows-connection）已有会话与出站队列。

## Goals / Non-Goals

- Goals: 入站 HID 帧注入本机；捕获转发到被控对端；切换热键；剪贴板双向（文本）；文件双向（≤16MiB）；延迟/键位屏真实数据；协议层零改动
- Non-Goals: 屏幕边缘切换、光标插值（附录 C）、非文本剪贴板、YAML 热重载、多显示器、管理员级注入隔离（UIPI 提升进程场景后续处理）

## Decisions

### D1. 注入走 VK（MapVirtualKey 取扫描码），不走 KEYEVENTF_SCANCODE
HID Usage 与 PS/2 Set-1 扫描码不是同一种编码；HID→VK 用静态映射表（§6.1 全集：字母/数字/功能键/导航/小键盘/修饰键），注入 KEYEVENTF_* 带 VK 派生的扫描码。修饰键作为独立 KEY 事件对（左右键区分 HIDKeyLCtrl..RMeta→VK_LCONTROL..VK_RWIN，右侧与导航键带 KEYEVENTF_EXTENDEDKEY）。测试只测映射表（纯函数），注入路径人工验证。

### D2. 捕获 = 低级钩子 + 专用消息泵线程；托管期吞键
WH_KEYBOARD_LL/WH_MOUSE_LL 回调要求泵线程；fgcore 用 runtime.LockOSThread + GetMessage 循环。托管对端时键盘/鼠标事件转 HID 帧发往对端并吞掉（LL 钩子返回 1 不透传），切换热键组合（Ctrl+Alt+0..8）除外——Ctrl+Alt+0 回本机，1..8 按实例名序选在线对端。非托管态钩子直通（返回 CallNextHookEx）。

### D3. 键位映射两层：RemapModifiers 恒启用 + KeyMapTable 按配置
捕获侧（本机 Win → 对端）：修饰键按 §6.3 RemapModifiers(from,to) 换算（Windows↔macOS 交换 Ctrl/Meta）；规则表从 `%APPDATA%\FlowGrid\keymap.yaml` LoadKeyMapFile，Lookup 命中则替换键码（§11），文件不存在 = 仅修饰键语义。注入侧（对端 → 本机）：对端已按它到本机的映射发过，本机只做 HID→VK（映射规则是对端发送者的责任）。

### D4. 剪贴板 = 每链接一个 ClipboardSession；本机变更广播全部在线对端
消息窗收 WM_CLIPBOARDUPDATE → CF_UNICODETEXT → UTF-8 → 各在线会话 SendClipboard("text/plain")（§5.3 上限 63240，超长截断并记日志）；入站重组完成 OnClipboard → 写本地剪贴板。回声抑制：本机写剪贴板后 N 秒内忽略监听回调。非文本 MIME 忽略。IPC `clipboard.set`（直接发送，冒烟/粘贴按钮用）与 `clipboard.text`（读本地，端到端断言用）。

### D5. 文件 = 每链接 FileSender/FileReceiver；入站自动接受到 downloads
入站 OFFER 直接 Accept，校验通过后落 `-data/downloads/<名>`（重名加序号）；Pump 由会话 50ms ticker 驱动（Pump(64)）。UI 文件屏：选文件 + 选在线对端发送，列表显示双向进度。IPC `file.send {instance,path}`（≤16MiB 拒绝更大数据，spec §7.4）、`file.transfers`、推送 `file.progress`。

### D6. 帧分发收敛在 link.session，hub 只消费回调
link.session 持有 clip/sender/receiver（emit=会话流），协议内部帧（剪贴板/文件）就地分发；HID 输入帧经 `Engine.OnPeerFrame(peer, frame)` 出来给 hub 注入/转发。Engine 增 `SendToPeer`、`BroadcastToPeers`、`OnlinePeerNames`。延迟统计（LatencyMonitor.Stats）进 LinkSnapshot。

## Risks / Trade-offs

- LL 钩子回调里有超时惩罚（系统可移除钩子）：回调只入队不做事，转发在独立 goroutine
- 注入对提权窗口（UAC 桌面）受 UIPI 限制——提升场景（服务+会话注入进程）留待 fgcored 里程碑
- 剪贴板仅文本（CF_UNICODETEXT）；图片/文件引用后续
- 入站注入与本地真实输入可能叠加（对端误发）——对方只在托管我们时发送，握手语义保证；异常叠加靠 DISCONNECT 兜底

## Open Questions

- 配对码/确认 UX 与自动接受的边界（文件已自动接受，剪贴板无条件同步——信任门之后即视为可信）
- SwitchLock 式独占热键（ScrollLock 双击）是否更符合习惯
