# FlowGrid Protocol Library

Go 实现的 FlowGrid 跨平台协议核心库。实现了 PROTOCOL.md 规范中的所有平台无关逻辑。

## 包结构

| 文件 | 说明 | 对应规范 |
|------|------|----------|
| `frame.go` | HID 帧编解码（12字节头 + 变长载荷）+ HMAC 完整性验证 | §5.1, §8.3 |
| `payload.go` | 各帧类型的载荷结构体和序列化 | §5.3 |
| `keymap.go` | HID Usage ID 表、修饰键掩码、跨平台修饰键重映射 | §6 |
| `keymap_rule.go` | YAML 键位映射规则文件加载和查询 | §11 |
| `identify.go` | IDENTIFY / IDENTIFY_RESPONSE 身份交换 | §4.2 |
| `crypto.go` | ECDH P-256 密钥交换、HKDF 会话密钥派生、公钥指纹 | §4.3 |
| `discovery.go` | BLE 广播数据、mDNS TXT 记录、RSSI 分级 | §3 |
| `wifidirect.go` | WiFi Direct 参数交换（20字节请求/8字节响应） | §3.1.3 |
| `apipa.go` | 以太网直连 APIPA /30 子网生成、重建与校验 | §3.2.2 |
| `sequence.go` | UInt16 序列号追踪、间隙检测、回绕处理 | §10.3 |
| `framing.go` | TCP Length-Prefix 封装、Raw Ethernet 帧、CRC32 | §7.2, 附录B |
| `fragment.go` | 帧分片发送和重组（5秒超时） | §7.3 |
| `heartbeat.go` | 心跳保活（2秒间隔）、延迟测量与连续高延迟告警计数 | §9 |
| `reconnect.go` | 重连状态机（指数退避 ±20% 抖动） | §10 |
| `errors.go` | 错误码常量、ProtocolError 类型、错误码名称表 | §12 |
| `platform.go` | 平台枚举、角色、能力标志位 | §2, §3.1.2 |

## 测试

```bash
cd protocol && go test ./...
```

## 使用方式

### 帧编解码
```go
frame := &protocol.HIDFrame{
    Header: protocol.FrameHeader{
        FrameType: protocol.FrameKeyDown,
        Sequence:  42,
        Timestamp: uint64(time.Now().UnixMicro()),
    },
    Payload: (&protocol.KeyDownPayload{
        KeyCode:   protocol.HIDKeyC,
        Modifiers: protocol.ModLeftCtrl,
    }).Marshal(),
}
data, _ := frame.EncodeBytes()
```

### 修饰键重映射
```go
// macOS Cmd+C → Windows Ctrl+C
mods := protocol.ModLeftMeta
remapped := protocol.RemapModifiers(mods, protocol.PlatformMacOS, protocol.PlatformWindows)
// remapped == ModLeftCtrl
```

### YAML 键位映射
```go
kmf, _ := protocol.LoadKeyMapFile("keymap.yaml")
table := protocol.NewKeyMapTable(kmf)
key, mods, found := table.Lookup(
    protocol.PlatformMacOS, protocol.PlatformWindows,
    protocol.HIDKeyC, protocol.ModLeftMeta, protocol.ContextGlobal,
)
```

### 延迟监控
```go
mon := protocol.NewLatencyMonitor(100)
// 发送 PING 时记录
mon.RecordPing(uint64(time.Now().UnixMicro()))
// 收到 PONG 时计算
sample := mon.RecordPong(pongTimestamp, uint64(time.Now().UnixMicro()))
// sample.ValueMs == 延迟毫秒
// sample.Jitter == 抖动
```

### 重连状态机
```go
rs := protocol.NewReconnectState(protocol.ReconnectCallback{
    OnReconnecting: func(attempt int, backoff time.Duration) {
        log.Printf("重连第 %d 次，等待 %v", attempt, backoff)
    },
    OnConnected: func() { log.Println("已连接") },
    OnIdle:      func() { log.Println("放弃重连") },
})
backoff, ok := rs.StartReconnect()
if ok {
    time.Sleep(backoff)
    // 尝试重连...
}
```

## 跨语言调用

通过 CGo 导出 C ABI（`go build -buildmode=c-archive`），Rust/C++/Swift/C# 可通过 FFI 调用。
KDE 客户端（Rust）即通过 `cgo/` 桥消费本库；句柄类对象（延迟监控、心跳、重连、序列号、
分片重组、键位表）由桥内的并发安全 registry 管理，多线程调用安全。
