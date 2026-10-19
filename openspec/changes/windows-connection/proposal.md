# Windows 对端连接（DirectLink TCP/TLS）

## Why

UI 壳已就位但设备只是「被发现」，还不是「可连接」。本变更让 fgcore 成为完整的 FlowGrid 节点：自通告（mDNS Advertiser）、TCP/TLS 监听与拨号、IDENTIFY 身份交换、心跳保活与重连——协议栈全部经 `github.com/flowgrid/protocol` 消费（原则 28），协议层零改动。这是键鼠注入里程碑的前置：连接先立起来，注入才有挂载点。

## What Changes

- **新增 `internal/link`**：连接引擎
  - 身份：首跑生成自签名证书（ECDSA P-256），持久 `-data` 目录；TOFU 信任库（对端证书指纹 + 名称）
  - 传输：DirectLink TCP 模式（§7.2），`crypto/tls` TLS 1.3（标准库，零第三方依赖），帧封装用 `protocol.AppendTCPFrame/ParseTCPFrames`
  - 会话：双向 IDENTIFY（§4.2，版本协商 + seq_init 交换）、心跳 2s×3 判丢（§9.1）、LATENCY_PING/PONG 采样（§9.2）、重连退避（§10.1）
- **自通告**：fgcore 启动 `protocol.MdnsAdvertisor`（platform=windows, caps=directlink,clipboard），发现侧过滤自身
- **IPC 扩展**：self.info / peers.list / peers.changed / connect.request / disconnect.request / pair.pending+accept+reject
- **UI 接线**：设备卡连接/断开按钮、配对确认提示（IDENTIFY 0x03 需确认时）、连接状态与延迟显示
- **Non-Goals**：键鼠注入与捕获、剪贴板/文件传输接线、NearLink（UDP+DTLS/WiFi Direct/BLE）、Raw Ethernet、0-RTT 恢复、APIPA、配对码 UX、防火墙规则自动注册

## Capabilities

### New Capabilities
- `windows-link`: DirectLink TCP/TLS 连接引擎的身份、信任、会话与重连语义

## Impact

- `windows/`：internal/link（新包）、internal/core（自身过滤 + IPC 分发扩展）、cmd/fgcore（flags 装配）、ui（连接操作接线）
- 协议层零改动；发现协议问题记 `docs/PROTOCOL-ISSUES.md`
- 验证：双进程环回冒烟（发现→连接→IDENTIFY→心跳→在线）+ 单测 + 全部既有测试仍绿
