# FlowGrid 协议对接指南（客户端/前端）

> 适用规范：`design/PROTOCOL.md` **v1.1**（线格式唯一权威）。
> 本文是面向客户端开发者的**对接标注文档**：怎么把传输接进来、回调怎么挂、
> 网络波动时协议库会做什么、你只需要画哪些界面。
> 最后更新：2026-10-16。

---

## 1. 总览：三层结构

```
┌─────────────────────────────────────────────┐
│ 你的客户端：GUI + 传输绑定（DTLS/TCP socket）│
├─────────────────────────────────────────────┤
│ 会话层（协议库已实现，直接用）                │
│  FrameStream        统一打序列号/时间戳       │
│  ClipboardSession   剪贴板分段/重组/去重      │
│  FileSender/Receiver 文件传输状态机+校验      │
├─────────────────────────────────────────────┤
│ 帧层（协议库已实现）                          │
│  HIDFrame 编解码 / 8 字节 HMAC / 序列号防重放 │
│  TCP 长度前缀 / Raw Ethernet / 分片(储备)     │
└─────────────────────────────────────────────┘
```

**你的职责**：① 把 socket 的 send/recv 接到库上（约 30 行胶水代码，见 §2）；
② 画界面、把回调接到控件上。**库的职责**：其余一切——分段、重组、去重、
重发、校验、超时、错误码。

---

## 2. 五分钟接入

### 2.1 发送方向：绑定传输

```go
// sink：把一帧交给你的传输（DTLS/TCP 都行，帧怎么上天线是你的事）
sink := func(f *protocol.HIDFrame) error {
    wire, err := f.EncodeBytesWithHMAC(sessionKey) // 8 字节 HMAC，见 §10
    if err != nil { return err }
    return myTransport.Write(wire)
}

stream := protocol.NewFrameStream(sink, seqTracker) // seqTracker 可与心跳共用
```

### 2.2 接收方向：分发循环

```go
// recvLoop: 每收到一个 UDP 包 / 一段 TCP 流，调用一次
func onWireBytes(wire []byte) {
    frame, _, err := protocol.DecodeFrameBytesWithHMAC(wire, sessionKey)
    if err != nil { return }                       // 坏帧/HMAC 不符：丢弃
    if err := seqTracker.Check(frame.Header.Sequence); err != nil {
        return                                     // gap/重放：丢弃，不回 ERROR
    }
    switch frame.Header.FrameType {
    case protocol.FrameClipboard:
        _, _ = clipIn.HandleFrame(frame.Payload)   // 内部去重/重组
    case protocol.FrameFileOffer, protocol.FrameFileChunk, protocol.FrameFileAck, protocol.FrameFileAbort:
        handled, err := fileIn.HandleFrame(frame.Header.FrameType, frame.Payload)
        _ = handled; _ = err
    case protocol.FrameHeartbeat:
        // 回 HEARTBEAT，见 PROTOCOL.md §9.1
    default:
        /* LATENCY_PING/PONG、IDENTIFY、DISCONNECT 等按需处理 */
    }
}
```

### 2.3 会话装配

```go
clipOut := protocol.NewClipboardSession(stream.Emit)
fileOut := protocol.NewFileSender(stream.Emit)
fileIn  := protocol.NewFileReceiver(stream.Emit)
```

---

## 3. 剪贴板同步

| 你调用 | 库做什么 |
|---|---|
| `clipOut.SendClipboard(mime, data)` | 分段 → 逐帧发送（1 轮） |
| `clipOut.SendClipboardPasses(mime, data, n)` | 同上，整流重发 n 轮（丢包/乱序传输用 ≥2） |
| `clipIn.HandleFrame(payload)` | 去重/乱序缓冲/换代/超时，完成时重组 |
| `clipIn.OnClipboard = func(c Clipboard)` | **GUI 挂点**：完整剪贴板到达（mime + data） |

行为保证（`protocol/clipboard.go`）：重复段忽略、乱序段按 Idx 缓冲、5 秒无进展
丢弃、孤儿段静默丢弃。**注意**：在可能乱序/丢段的传输上必须用
`SendClipboardPasses(…, ≥2)`——载荷无传输 ID，单轮无法区分迟到副本与重启。

---

## 4. 文件传输

### 4.1 发送方（GUI：选择文件 → 进度条 → 结果）

```go
fileOut.OnProgress = func(id uint16, sent, total int) { 更新进度条 }   // 可选
fileOut.OnDone     = func(id uint16, err error)        { 提示成功/失败 } // 可选

id, err := fileOut.OfferFile("report.pdf", data) // ≤16MiB；哈希自动计算
for 需要发送 && 未完成 {
    n, err := fileOut.Pump(8)  // 事件循环里每次最多发 8 帧；n==0 表示本轮无事
    ...
}
```

### 4.2 接收方（GUI：弹窗 → 接受/拒绝 → 完成）

```go
fileIn.OnOffer = func(o protocol.FileOffer) {
    弹窗(o.Name, o.Size)            // 必须在 30s 内调用 Accept 或 Reject
}
fileIn.OnComplete = func(o protocol.FileOffer, data []byte, err error) {
    if err == nil { 落盘(data) }    // SHA-256 已校验通过
    else { 提示失败原因(err) }
}
fileIn.Accept(id)        // 或 Reject(id, protocol.FileRejectedUser)
fileIn.Abort(id, protocol.FileAbortUser)   // 任意时刻取消
```

### 4.3 状态机与保障

```
Sender:  OFFER →(ACK accepted)→ CHUNK 流(Pump, 丢段自动重发 ≤2 轮)
              →(ACK complete)→ 完成
Receiver: OFFER →(用户 Accept)→ 缓存 CHUNK → SHA-256 校验 → ACK complete → 交付
超时:  OFFER 30s 未确认作废；传输 10s 无进展失败（stale 状态自动过期，不卡死）
```

---

## 5. 网络波动行为表（库的确定性行为）

| 网络现象 | HID/心跳帧 | 剪贴板 | 文件传输 |
|---|---|---|---|
| 丢包 | Sequence gap 报错并重同步；**帧不补发**（HID 新状态覆盖旧状态） | 丢段 → 下轮重发修复 | Pump 自动重发 ≤2 轮修复 |
| 乱序 | 落后帧按重放丢弃 | 按 Idx 缓冲 | 按 ChunkIdx 缓冲 |
| 重复 | 按重放丢弃 | 静默忽略 | 静默忽略 |
| 迟到副本（完成后到达） | 按重放丢弃 | 静默丢弃（不产生 ghost 缓冲） | 静默丢弃 |
| 对端停滞 | 心跳 6s 判死 → 你的重连流程 | 5s 无进展丢弃整份 | 10s 无进展失败；stale 接收态自动过期 |
| 坏帧/篡改 | HMAC 校验失败丢弃（连续 3 次应断链） | 同左（传输层之下） | 同左 |
| 巨包攻击 | 解码全路径长度校验，分配有界（分片 32 组上限） | 同左 | 同左 |

**你需要做的**：NearLink（UDP）上发送剪贴板用 ≥2 轮；心跳超时后走你自己的
重连；其余库兜底。

---

## 6. 常量与限制速查

| 项 | 值 | 出处 |
|---|---|---|
| 帧头 | 12 字节；Payload ≤ 255 | PROTOCOL.md §5.1 |
| 帧最大 | 267 字节（12+255） | §5.1 |
| HMAC 尾 | 8 字节（HMAC-SHA256 截断） | §8.3 |
| TCP 记录 | 2 字节长度前缀（含 HMAC 尾），单记录 ≤ 275 | §7.2 |
| 剪贴板单段 | ≤ 255-(7+M) 字节（text/plain ≈ 238） | §5.3 |
| 剪贴板总量 | ≤ 255×(248-M)；空 MIME 63240 字节 | §5.3 |
| 剪贴板超时 | 5s 无进展 | §5.3 |
| 文件上限 | 16 MiB | §7.4 |
| 文件单块 | ≤ 249 字节 | §7.4 |
| OFFER 超时 / 传输空闲超时 | 30s / 10s | §7.4 |
| 重发轮次 | 默认额外 2 轮（SetResendPasses 可调） | §7.4 |
| 心跳 | 2s 间隔，3 次超时 | §9.1 |
| 重连 | 5 次，200ms×2 退避，上限 5s，±20% 抖动 | §10.1 |

---

## 7. 错误码（§12）

传输层 0x01-0x0E、协议层 0x10-0x16、HAL 层 0x30-0x34、断开原因 §12.4，
全表见 PROTOCOL.md §12。代码内常量：`protocol.Err*`（如 `ErrHmacMismatch`
= 0x10、`ErrInvalidPayload` = 0x12、`ErrVersionMismatch` = 0x13、
`ErrTransferTimeout` = 0x15、`ErrTransferRejected` = 0x16、
`ErrSequenceGap` = 0x20）。用 `protocol.ErrorCodeString(code)` 取规范名。

---

## 8. cgo 集成（Rust/C 客户端）

- 头文件 `protocol/cgo/cgo.h`；构建产物 c-archive（需 CGO 环境，见 AGENTS.md）
- **句柄从 1 开始**，0 永远表示失败/空
- 所有返回缓冲用 `fg_free_buffer` 释放，**且只释放一次**
- 字符串入参可为 NULL（按空串处理）；`out*` 出参必须传有效指针
- `fg_latency_record_pong` 返回 -1 表示该 PONG 被拒（未来时间戳/不匹配），丢弃即可
- 注意：会话层（clipboard/filetransfer）暂无 cgo 导出——新增导出需在 Linux
  上重新生成 cgo.h；在此之前 Rust 客户端可走帧级 API 自行装配，或等导出补齐

---

## 9. 线格式速查

```
帧头 (12B, 大端):  Type(1) Seq(2) Timestamp(8, µs) PayloadLen(1) Payload(N)
HMAC 尾 (8B):     HMAC-SHA256(key, 12+N 帧字节)[0:8]
TCP 封装:         Length(2, 含 HMAC) + Frame + HMAC
Raw Ethernet:     Dst(6) Src(6) EtherType(0xF1A0) Frame [CRC(4)] [HMAC(8)]

常用载荷:
 KEY_DOWN  KeyCode(2, BE) Modifiers(1)      修饰键位掩码见 §6.2
 MOUSE_MOVE DeltaX(2,s16) DeltaY(2,s16)
 HEARTBEAT 无载荷，收到即回
 IDENTIFY  Ver(1) Platform(1) Role(1) Caps(1) SeqInit(4) Name(\0 结尾)
 CLIPBOARD M(1) MIME(M) TotalSize(4) FragIdx(1) FragTot(1) Data(N)
```

---

## 10. 文件索引

| 关心什么 | 去哪看 |
|---|---|
| 线格式权威定义 | `design/PROTOCOL.md`（v1.1） |
| 帧编解码/完整性 | `protocol/frame.go`、`protocol/framing.go` |
| 序列号/心跳/重连 | `protocol/sequence.go`、`heartbeat.go`、`reconnect.go` |
| 剪贴板会话 | `protocol/clipboard.go`（测试即行为说明：`clipboard_session_test.go`） |
| 文件传输 | `protocol/filetransfer.go`（`filetransfer_test.go`） |
| 网络波动韧性 | `protocol/network_resilience_test.go`（丢包/乱序/孤儿场景） |
| 仓库工作约定 | `AGENTS.md` |
