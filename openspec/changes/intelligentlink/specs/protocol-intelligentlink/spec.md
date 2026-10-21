# protocol-intelligentlink

## ADDED Requirements

### Requirement: LAN 多播通告与识别

设备在局域网多播组（239.255.42.99:24801）周期广播签名通告（Version + Platform + Caps + Port + Name + IdentityPubKey + ECDSA-P256 签名）；监听方收通告后用通告内公钥验签，通过即识别为候选对端（记录 name/platform/caps/端点/指纹/lastSeen）；10s 未更新自动淘汰。验签失败静默丢弃。

#### Scenario: 通告往返识别

- **WHEN** 设备 A 广播通告，设备 B 的 Listener 收到
- **THEN** B 验签通过，识别列表出现 A（含 A 的 TCP 端点与指纹）；A 停止广播 10s 后 B 的列表中 A 被淘汰

#### Scenario: 篡改通告被拒

- **WHEN** 监听方收到签名被篡改的通告
- **THEN** 验签失败，通告被静默丢弃，不进入识别列表

### Requirement: 私有链信任（局域网私有区块链）

每设备维护哈希链式签名信任日志：记录 {对端身份, 指纹, 决策, 时间戳, prevHash, 签名}，只追加。首接触展示指纹待人工确认后追加 trust 记录；后续验签通过且指纹命中链上最新 trust 记录即自动信任；指纹不命中时提示确认、不自动连接。链上任一记录被篡改（prevHash 断裂）可被校验发现。

#### Scenario: 首接触信任

- **WHEN** Listener 首次识别到对端且其指纹不在信任链上
- **THEN** 对端进入「待确认」状态，UI 展示指纹；用户确认后追加 trust 记录

#### Scenario: 已信任对端自动识别

- **WHEN** 已信任对端再次广播且验签通过、指纹命中链上最新 trust 记录
- **THEN** 对端直接进入「已信任」状态，可发起连接

#### Scenario: 链式防篡改校验

- **WHEN** 校验信任链时发现某条记录的 prevHash 与前条不匹配
- **THEN** 校验失败，该链被视为已损坏（实现侧告警，不静默信任）

### Requirement: 两模型链路选择

连接入口按策略排序：直连（mDNS 对端）优先、智能连接（LAN 识别对端）兜底。preferredLink = IntelligentLink 优先时先 LAN 后 mDNS；DirectLink 优先时先 mDNS 后 LAN；仅 IntelligentLink 时只走 LAN，无对端明确报错。

#### Scenario: 无 BLE、mDNS 不可用

- **WHEN** 设备无 BLE 且 mDNS 无对端，但 LAN 多播识别到已信任对端
- **THEN** 按策略经智能连接路径发起 TCP 连接，会话正常建立

#### Scenario: 仅 IntelligentLink 且无对端

- **WHEN** preferredLink = 仅 IntelligentLink 且 LAN 无识别对端
- **THEN** 明确报错「未发现智能连接对端」，不静默降级到其他路径
