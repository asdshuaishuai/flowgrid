## Context

协议层已有：帧编解码/会话层、mDNS 浏览器（mdns.go，Listener 状态机模板）、指纹计算（crypto.go PublicKeyFingerprint）、APIPA（apipa.go）。mac 客户端：DirectLink TCP + 配对码 HMAC；Windows：DirectLink TCP + TLS；deepin：骨架。`preferredLink` 已持久化但无连接代码读取。

## Goals / Non-Goals

- Goals: 智能连接（UDP 多播通告 + ECDSA 身份 + 私有链信任）在协议层落地；两模型选择生效；三平台经 cgo/直引共享同一实现；无 BLE、mDNS 不可用时全功能
- Non-Goals: BLE 射频、REA（Raw Ethernet）、TLS/ECDH 升级、移动端、真 AirDrop 互通

## Decisions

### D1. 通告记录与多播参数

记录：`Version(1) + Platform(1) + Caps(1) + Port(2 BE) + NameLen(1) + Name(≤64) + IdentityPubKey(65) + Signature(64)`；签名 = ECDSA-P256-SHA256 覆盖除签名外全部字段。多播组 239.255.42.99:24801（239.255.x.x 本地范围，避开 SSDP 1900；TTL=1 不出局域网）；通告周期 2s（与心跳同频）。端口即设备 DirectLink TCP 监听端口；UDP 源 IP 即对端 IP。

### D2. 身份与验签（私有链的密码学地基）

每设备持久身份密钥对（P-256；协议层生成，客户端安全存储：Keychain/DPAPI/keyring）。Listener 收通告 → 用通告内 IdentityPubKey 验签 → 通过 = 识别候选。指纹 = hex(SHA-256(IdentityPubKey)[0:16])（复用 crypto.go）。验签失败/篡改 → 静默丢弃（网络噪声）。

### D3. 私有链信任日志（局域网私有区块链）

信任链 = 哈希链式签名日志：每条记录 `{对端身份, 指纹, 决策(trust/reject), 时间戳, prevHash, 自身签名}`。性质：签名防伪、prevHash 链式防篡改、只追加（撤销 = 追加新记录而非改写）、去中心（每设备本地链，无需共识——即「局域网私有区块链」）。首接触：UI 展示指纹 → 人工确认 → 追加 trust 记录；后续：验签 + 指纹命中链上最新 trust → 自动信任；不命中 → 提示确认，不自动连接。

### D4. UDP+TCP 协同

UDP 管发现与识别（轻量、无连接、广播语义）；TCP 管数据传输（可靠、有序，复用现有 DirectLink 会话：IDENTIFY 握手 + 帧 HMAC）。识别 + 信任后直连对端 IP:Port。不做 UDP 数据通道（HID 低延迟是 §7.1 的议题，本次简化不做）。

### D5. 两模型链路选择

优先级：直连（mDNS 可用）→ 智能连接（兜底）。`preferredLink`：IntelligentLink 优先 = 先 LAN 识别后 mDNS；DirectLink 优先 = 先 mDNS 后 LAN；仅 IntelligentLink = 只走 LAN，不可用明确报错不静默降级。选择逻辑纯函数（输入：mDNS 对端数 / LAN 对端数 / 手动地址 / 策略），表驱动可测。

### D6. 协议层与客户端的边界

协议层（Go）：记录编解码、签名验签、状态机、信任日志、选择逻辑——全部可单测。客户端：UDP socket 绑定（纯 UDP，无射频依赖）、身份密钥与信任链持久化、指纹确认 UI。BLE 射频不在本次范围。
