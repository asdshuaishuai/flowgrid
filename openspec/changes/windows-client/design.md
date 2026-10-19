## Context

本机（Windows 11 22H2，2026-10 盘点）：Go 1.25.6（CGO_ENABLED=0，无 gcc → cgo 不可用，FFI 消费面全断）；.NET SDK 8.0.413 + WindowsDesktop.App 8.0.19（WPF 探针项目实测离线构建 5.5s 零警告）；MSVC 14.44 在 BuildTools 里但 WinUI 3 缺 Windows App SDK；Rust 为 gnu 工具链。协议栈唯一可行消费方式是 **Go 直引协议库**（原则 28）。deepin/ 已回归（DTK6 8 屏），其 designtokens.h 是「设计系统移植到原生令牌」的既有先例。

## Goals / Non-Goals

- Goals: 进程边界与 IPC 契约定型；UI↔核心↔mDNS 端到端竖切可跑；后续里程碑（输入/连接/传输/皮肤）可增量叠加不推倒
- Non-Goals: fgcored 服务层、输入注入/捕获、DTLS 连接、剪贴板/文件接线、UI 皮肤化（本次全部不做）

## Decisions

### D1. 目标三层形态，MVP 只做两层
终态：`fgcored`（SYSTEM 服务：自启、会话监听、拉起会话核心）→ `fgcore`（会话核心：协议栈 + 输入注入/捕获，提权）→ `FlowGrid.UI`（WPF，用户会话）。MVP 先落 `fgcore` 单进程 + UI；fgcored 推迟到输入里程碑——注入尚未存在，服务层现在无活可干（YAGNI），但分层边界从第一天就按终态设计，避免日后痛苦重构。

### D2. C# 全程不碰 wire format
协议栈（FrameStream / ClipboardSession / FileSender / mDNS / 未来的注入引擎）独占于 Go 核心；IPC 只传**语义消息**（设备列表、状态、进度）。这是原则 28 「直接引用 Go 库」路径的进程化形态，UI 断线重连不丢协议状态（状态全在核心）。

### D3. IPC = 命名管道 + JSON Lines 信封
传输：`\\.\pipe\flowgrid-core`（Go 侧 go-winio v0.6.2（stdlib 无 `"pipe"` 网络；v0.6.4 需 go≥1.26 故钉 v0.6.2）↔ C# 侧 `NamedPipeClientStream`）。消息：每行一个信封 `{"v":1,"type":...,"id":...,"payload":{...}}`，请求-响应用 `id` 关联，事件无 `id`。选 JSON 而非二进制：UI 内部通道带宽极低、可调试性优先；`v` 字段留演进。消息量增大后再评估紧凑编码（Open Question）。

### D3b. 出站写一律走每连接发送队列（为故障设计）
winio 以零配额创建管道时（`PipeConfig` 缓冲为 0），对端没有 pending read 的写入会**永久阻塞**（探针实测复现）。两条对策：`core.ListenPipe` 显式给 64KB 内核缓冲；连接出站帧全部经 `pipeConn.post` 非阻塞入队（64 帧深），由唯一 writeLoop 落盘，队列满即断开该客户端——慢 UI 不许拖死核心循环（发现/广播跑在核心 goroutine 上）。

### D4. UI 无状态 + 拉全量 + 推增量
UI 不持协议状态：每次连接 `hello` → `devices.list` 拉全量快照，此后核心 `device.changed` 推送全量快照（快照语义而非 diff，UI 侧零合并逻辑）；断线按固定退避自动重连，重连成功重新拉全量。

### D5. 剪贴板监听窗归 UI 进程（后续接线时的边界背书）
AddClipboardFormatListener 需要交互会话窗口，WPF 天然满足；收到剪贴板经管道推核心、由核心走 ClipboardSession 发送。本次不实现。

## Risks / Trade-offs

- 命名管道用默认 DACL，同用户会话内够用；fgcored 阶段必须显式 ACL（届时升级 windows-ipc spec）
- ~~Go stdlib 的 `net.Listen("pipe", ...)` 待验证~~ → 已落定：stdlib 不支持，传输层用 go-winio v0.6.2（纯 Go，零 cgo）；零配额写阻塞坑见 D3b，冒烟与单测均已覆盖
- 双进程对 MVP 是提前量，但会话/服务拆分是产品刚需（D1），晚拆成本更高
- mDNS 组播在部分网段不可用 → 核心降级运行，status 如实上报，UI 显示可观测

## Open Questions

- 会话核心的注入进程提权方式（服务拉起提权进程 vs UIAccess 清单）——输入里程碑设计
- IPC 二进制化（长度前缀 + 紧凑编码）的触发阈值——等消息集变大再评估
