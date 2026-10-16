## Context

审计确认的缺陷分三类：内存安全（分片越界 panic、cgo UAF）、协议正确性（载荷截断、CLIPBOARD 不可达目标）、安全机制（可暴破的 4 字节 HMAC、无防重放、PSK 哈希广播）。修复横跨 wire format 与实现层，且部分为不兼容变更。

## Goals / Non-Goals

- Goals: wire format 自洽（规范零矛盾）、协议库内存/并发安全、剪贴板与文件传输协议化（客户端只写 GUI）、错误码接线
- Non-Goals: 不实现传输加密（DTLS/TLS 属客户端传输层）；不做大文件（>16MiB 需未来帧格式）；不做统一握手状态机（等客户端定型）

## Decisions

### D1. HMAC 截断 8 字节（取代 4 字节归档决策）
64-bit MAC 使在线伪造需 ~2^64 次。4 字节在 1M pps 下约 71 分钟可伪造一帧。 NearLink 有 DTLS 时帧 HMAC 冗余但无害；HMAC-only 链路（Raw Ethernet）是唯一防线，必须够强。失败即断链（连续 3 次）防暴破。

### D2. CLIPBOARD 载荷内嵌分段，废除帧外分片
v1.0 帧外分片服务于"超过 MTU 的帧"，但 UInt8 载荷上限使合法帧 ≤267 字节，机制永远不触发；64KB 剪贴板正确做法是对载荷本身分段。分段字段（TotalSize/Idx/Tot）置于 MIME 之后、Data 之前，总上限 = 255×248 = 63240（64KB 在 UInt8 分段数下不可达，一并修正）。

### D3. 序列号 stale 拒绝 + 静默丢弃
重放帧（diff ≥ 0x8000）从"返回 nil 放行"改为返回错误；调用方必须丢帧且不回 ERROR（防错误风暴放大）。gap 与 stale 同码（ErrSequenceGap）但 Message 可区分；是否升级为结构化区分留待客户端定型。

### D4. 会话层放协议库，客户端只写 GUI
FrameStream 接缝（sink + seq + 时钟）之上，ClipboardSession/FileSender/FileReceiver 封装全部分段、重组、去重、超时、校验状态机。回调一律锁外触发（可重入）。超时全部惰性求值（无 goroutine/定时器），时钟可注入。

### D5. 客户端代码整体移除
kde/（Rust workspace + cxx-qt + QML）与 deepin/（DTK C++ + Rust C ABI staticlib）共 76 个跟踪文件删除。理由：wire format 重写后旧客户端全面脱节，所有者决定重写；新客户端直接消费会话层。经验保留在 git 历史（e8d205c、d1e71e4、6b9aafa）。

## Risks / Trade-offs

- cgo 子包本机（Windows，无 gcc）无法编译验证 → 桥接改动以语法校验 + Linux 复核为代价（见 tasks 验证节）
- 保留 fragment.go 与"§7.3 废除"并存 → 已在规范标注"储备实现，不在任何路径使用"
- 协议版本号保持 1 → §2.3 澄清"v1.0 从未发布"，未来不兼容变更必须升号

## Open Questions

- 新客户端技术栈（Qt/Rust/GTK？）未定，影响 cgo 导出集合的最终形态
