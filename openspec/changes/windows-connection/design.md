## Context

PROTOCOL.md 定案：DirectLink = TCP 24801 + TLS 1.3 + 2B 长度前缀封装（§7.2）；帧完整性 HMAC 在 DirectLink-TCP「建议启用」非强制（§8.3）；配对信任 = 自签名证书指纹 TOFU（§4.3/§8.1）；IDENTIFY 双向交换含版本协商与 seq_init（§4.2，首帧必须等于 seq_init，§10.3）；心跳 2s×3 判丢（§9.1）；重连 5 次 200ms×2 退避 ±20% 抖动（§10.1）。协议库已有 AppendTCPFrame/ParseTCPFrames/SequenceTracker/HeartbeatTracker/LatencyMonitor/Identify 编解码，客户端只写传输与状态机。

## Goals / Non-Goals

- Goals: 双 fgcore 环回（发现→TLS→IDENTIFY→心跳→在线）全链路可跑；信任模型落地；IPC/UI 可驱动可观测；协议层零改动
- Non-Goals: 注入/捕获、剪贴板/文件接线、NearLink 全家（UDP+DTLS、WiFi Direct、BLE）、Raw Ethernet、0-RTT、APIPA、防火墙自动注册、配对码 UX

## Decisions

### D1. 传输选 DirectLink TCP + TLS 1.3（stdlib），NearLink 全部后置
`crypto/tls` 原生 TLS 1.3（AEAD 完整性 + ECDHE-P256，恰好是 §8.1 的套件族），零第三方依赖；帧封装用库内 `AppendTCPFrame/ParseTCPFrames`。NearLink 需 pion/dtls + Windows WiFi Direct API，独立里程碑。UDP 24801 只在 NearLink 用，本节点只听 TCP 24801。

### D2. 帧 HMAC 暂不启用（§8.3 允许）
TLS 1.3 记录层已是 AEAD 完整性；HMAC 在 DirectLink-TCP 为「建议」非「必须」。不启用避免了「TLS 之上再取 session_key」的派生设计（§4.3 的 ECDH 派生路径面向 BLE/Raw Ethernet）。后续若启用，用 TLS exporter 派生并对账规范。AppendTCPFrame/ParseTCPFrames 传 nil key 即为无 HMAC 记录。

### D3. 身份 = 自签证书 + 固定根钉扎（零校验旁路）
首跑生成 ECDSA P-256 自签证书（CN/SAN=flowgrid.local，10 年）持久于 `-data`。信任库持久对端**证书 DER**；拨号方把对端证书放进 `RootCAs` 走 Go 标准链校验（自签证书作为自身根，只有「确系那台对端」能通过）——代码中不存在 `InsecureSkipVerify`，安全扫描（Mimosa）与本仓库安全原则（27）双合规。指纹 = hex(SHA-256(证书 DER)[0:16])，注意与 §4.3 的「ECDH 公钥指纹」（BLE/ECDH 配对路径）不是同一约定，TLS 路径按 §8.1「信任证书指纹」。TLS 双向出示证书：服务端 `RequireAnyClientCert`（不验链、只取指纹），两侧连接键对称。

### D4. 确认门：入站 0x03 + UI 裁决；出站须先配对
对端连接入站时，未信任对端收 IDENTIFY_RESPONSE 0x03（需用户确认），UI 弹裁决，30s 超时按拒绝；确认后指纹+证书入库。`-auto-accept`（开发/无人值守）自动信任。**出站（拨号方）没有 TOFU 首连**：无对端证书则固定根无从建立，Connect 直接拒绝——首次配对的证书分发是协议规范空缺（mDNS TXT 无证书字段，PROTOCOL-ISSUES P3），当前以 `-peer-cert`/`-peer-name` 带外导入引导，规范层解法待协议修订。

### D5. 会话状态机（每连接一条 goroutine 对）
写侧：出站队列（64 帧深，非阻塞投递，慢对端溢出即断——与 IPC pipeConn 同款防拖死设计）→ AppendTCPFrame → bufio 写。读侧：积累缓冲 → ParseTCPFrames → 分发：HEARTBEAT（回帧 + tracker 喂狗）/ LATENCY_PING（回 PONG 原样 T1）/ LATENCY_PONG（LatencyMonitor）/ DISCONNECT（收尾）/ 其余帧计数忽略（注入里程碑接管）。心跳 ticker 2s 调 HeartbeatTracker.Tick，丢失即断链并触发重连。

### D6. 重连按 §10.1 的 5 次×200ms×2 ±20%
期望在线的对端（用户 connect.request 过的）断链后自动重连；成功后重新 IDENTIFY（新 seq_init，不沿用旧序列号空间，§10.2.4）。重连用发现表最近地址，过期则回退等 mDNS 刷新。

### D6b. 心跳：双侧各自周期发送、收帧即喂狗、不回帧
§9.1 字面上的「接收方收到后立即回复」叠加「双方各自每 2s 周期发送」会在 Role=Both 下产生 1:1 回显放大——实测 36s 内 29.5 万帧的指数风暴（PROTOCOL-ISSUES P4）。实现取语义 (b)：两侧各自 tick 发送，收帧仅喂狗（`ReceivedHeartbeat`），丢 3 拍判死不变。延迟 PING/PONG 无此问题（PING 只由采样方发起，PONG 只回给采样方）。

### D7. 自通告 + 自身过滤 + 同机环回限制
fgcore 起 `MdnsAdvertisor`（instance=`<name>._flowgrid._tcp.local.`，host=净化名.local，TXT: version=1/platform=windows/caps=directlink,clipboard/port=<tcp>/encryption=tls13）。发现侧 Core 按实例名过滤自身通告。TXT encryption 用扩展值 `tls13`——规范只枚举了 dtls13/none，TCP-TLS 无值可选，已记 PROTOCOL-ISSUES P2。**同机双进程的 mDNS 环回在 Windows 上不可靠**： advertiser 按查询源地址单播应答，同端口的另一个套接字可能截走应答——真双机局域网无此问题（协议库 mdns 单测覆盖解析路径）；同机联调用 `devices.add` IPC 注入地址（也是「手动添加设备」功能面）。

### D8. IPC 面扩展
`self.info` / `peers.list`→`peers` / `peers.changed` 推送（全量快照）/ `connect.request {instance}` / `disconnect.request {instance}` / `pair.pending` 推送 + `pair.accept|pair.reject {id}`。引擎与发现表解耦：引擎经注入的 resolver 从发现表拿地址。

## Risks / Trade-offs

- 证书指纹与 §4.3 公钥指纹语义分叉（TLS 路径 vs ECDH 路径）——若未来统一，需规范层裁决
- 30s 确认窗口内 pending 状态进程崩溃即丢（pending 不持久化）——重连重来一遍，可接受
- 首次真实 LAN 部署需手动放行 TCP 24801 入站（防火墙自动注册 Non-Goal）
- TLS 层不校验客户端证书，对端合法性由 IDENTIFY 确认门把关——若要求 TLS 双向认证，后续加固

## Open Questions

- 帧 HMAC 启用时 session_key 的派生源（TLS exporter 细节）与规范对账
- 配对码 UX（6 位数字）与 IDENTIFY 0x03 确认门如何结合
