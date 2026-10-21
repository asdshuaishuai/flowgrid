# 智能连接（局域网私有区块链）+ 直连 两模型

## Why

「优先链路」设置（IntelligentLink 优先 / DirectLink 优先 / 仅 IntelligentLink）目前是占位——没有任何连接代码读取它。同时，设备发现依赖 mDNS（Bonjour/Avahi）：没有 BLE 且 mDNS 不可用（AP 隔离、mDNS 被禁、非 Bonjour 网络）时，设备无法互相发现，数据传递与操控无从开始。

本变更构建**智能连接**：以去中心服务（每设备自带通告器 + 监听器，无服务器）在局域网 IP 层构建**私有链识别**——设备间经密码学签名确认身份、经指纹确认建立信任的私有链路——为无 BLE、mDNS 不可用的设备兜底。与既有**直连**（mDNS + TCP）构成两模型，`preferredLink` 真正生效。

## What Changes

- **新增 `protocol/lanid.go`**：LAN 识别协议——多播通告记录（Version + Platform + Caps + Port + Name + IdentityPubKey + ECDSA-P256 签名）、Announcer/Listener 状态机（周期通告 / 收通告验签聚合 / 过期淘汰，镜像 mdns.go browser）、私有链信任日志（哈希链式签名记录：对端身份 + 指纹 + 决策 + 时间戳 + prevHash + 签名）
- **新增 `protocol/link.go`**：两模型链路选择——直连（mDNS 可用）优先、智能连接兜底；preferredLink 三策略纯逻辑
- **cgo 导出**：`fg_lanid_announcer_*` / `fg_lanid_listener_*` / `fg_lanid_trust_*` / `fg_link_plan`
- **PROTOCOL.md 升级**：§1 双协议表 → 两模型；§3 补「智能连接——LAN 私有链发现」；NearLink → IntelligentLink 术语同步（内容零变更）
- **客户端薄适配器**：mac（UDP 多播 + 信任流 UI）、Windows fgcore（UDP 多播，Go 直引协议层）、deepin（UDP 多播，cgo）
- **Non-Goals**：BLE 路径、REA 模式、ECDH/TLS 安全升级（后续迭代）；移动端；与真 AirDrop 比特级互通

## Capabilities

### New Capabilities
- `protocol-intelligentlink`: LAN 多播通告与识别、ECDSA 身份验签、私有链信任日志、两模型链路选择

## Impact

- `protocol/`：新增 lanid.go / link.go；cgo 导出扩展；表驱动单测
- `design/PROTOCOL.md`：§1/§3 升级 + 术语同步
- `mac/`：DirectLinkManager 接 LinkSelector；UDP 多播适配器；信任流 UI（指纹确认 + 信任库）
- `windows/`、`deepin/`：UDP 多播薄适配器（各自开发机验证）
- 线格式零变更（纯发现层扩展）；既有 DirectLink 路径行为不变
