## Why

协议库安全审计（5 维并行审查 + PoC 验证）确认多项规范级缺陷，且部分修复涉及线上不兼容变更。趁客户端实现整体移除重写（无存量对端）之机，一次性完成 wire format 修订，使 PROTOCOL.md v1.1 成为自洽、可独立实现的契约。

核心动因：
- **分片重组越界 panic**：两个伪造分片即可打崩进程（PoC 实锤 `index out of range [200] with length 2`）
- **载荷长度回绕**：>255 字节载荷静默截断长度字节，产出损坏帧（PoC 实锤 300→44）
- **CLIPBOARD 64KB 目标数学上不可达**：UInt8 载荷上限 vs 64KB 需求矛盾，v1.0 分片机制从未有合法触发路径
- **4 字节截断 HMAC 可在线暴破**（1M pps 下约 1 小时伪造一帧）
- **无防重放**：旧帧静默放行；**BLE 广播 PSK 哈希前缀**构成离线字典攻击面

## What Changes

- **帧完整性**：HMAC-SHA256 截断 4 → 8 字节（§8.3），新增连续失败断链要求；**取代归档 spec `frame-integrity-hmac` 的 4 字节决策**
- **CLIPBOARD v1.1**：载荷内嵌分段（Total Size / Frag Idx / Frag Tot，§5.3），总上限修正为 255×248=63240 字节；废除 §7.3 帧外分片机制（保留加固实现作储备，不再使用）
- **新增 §7.4 文件传输协议**：FILE_OFFER/ACK/CHUNK/ABORT（0x21-0x24），≤16MiB，SHA-256 校验后交付
- **序列号语义澄清**（§10.3）：首帧须等于 seq_init；重复/过期帧必须静默丢弃、不回 ERROR
- **移除 BLE 参数交换中的 PSK Hash**（§3.1.3，Byte 15-18 保留置零）
- **错误码**：0x15 fragmentationTimeout → transferTimeout；新增 0x16 transferRejected
- **版本语义澄清**（§2.3）：Protocol Version 1 即本文档线格式（v1.0 从未对外发布）
- **协议库新增会话层**：session.go（FrameStream）、clipboard.go（ClipboardSession）、filetransfer.go（FileSender/FileReceiver），供未来客户端直接消费
- **移除客户端实现**：kde/、deepin/ 整体删除，待重写（git 历史 e8d205c/d1e71e4 可考）

## Capabilities

### New Capabilities
- `file-transfer`: FILE_OFFER/ACK/CHUNK/ABORT 状态机、16MiB 上限、SHA-256 校验后交付
- `clipboard-segmentation`: 载荷内分段、接收侧重组/去重/换代/5s 超时

### Modified Capabilities
- `frame-integrity-hmac`: 4 字节截断 → 8 字节截断（本文档取代 2026-06-29 归档中的对应决策）

## Impact

- **Go protocol**: 全部核心文件（frame/framing/fragment/sequence/heartbeat/payload/identify/discovery/keymap/apipa/wifidirect/errors）+ 新增 session/clipboard/filetransfer + cgo/bridge 加固
- **design/PROTOCOL.md**: 升版 v1.0 → v1.1（修订记录 10 条）
- **移除**: kde/、deepin/ 全部客户端代码（76 文件）
- **兼容性**: 线上不兼容变更（HMAC 长度、CLIPBOARD 布局），因无存量对端而无迁移成本；Protocol Version 保持 1 并在 §2.3 澄清语义
- **验证**: `cd protocol && go test -count=1 .` 全绿（110+ 用例，含分片越界/拷贝语义/截断拒绝等回归测试）；cgo 子包需 gcc 环境验证
