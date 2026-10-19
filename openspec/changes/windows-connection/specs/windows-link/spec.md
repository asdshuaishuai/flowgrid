# windows-link

## ADDED Requirements

### Requirement: 身份与信任

fgcore 首次启动生成自签名证书（ECDSA P-256）并持久化于数据目录；对端以证书指纹（hex(SHA-256(证书 DER)[0:16])）标识。信任库持久化指纹与对端名。

#### Scenario: 首次连接未信任对端（入站）

- **WHEN** 收到未入信任库的对端连接（或对端证书未信任）
- **THEN** 未启用自动信任时回 IDENTIFY_RESPONSE 0x03 并推送 pair.pending，30 秒未决按拒绝处理；确认后回 0x00 并持久化指纹与证书

#### Scenario: 未配对对端的出站连接

- **WHEN** UI 对信任库中无证书的对端发起 connect.request
- **THEN** 拒绝并报错（固定根无从建立）；首次配对走带外证书导入，规范层解法见 PROTOCOL-ISSUES P3

#### Scenario: 已信任对端重连

- **WHEN** 对端指纹已在信任库
- **THEN** 直接接受（0x00）并刷新最近地址；证书与固定根不符的连接在 TLS 握手层即失败

#### Scenario: 自动信任模式

- **WHEN** 以 -auto-accept 启动
- **THEN** 未信任对端自动入库并接受（仅限显式 flag 的开发/无人值守场景）

### Requirement: 会话建立（IDENTIFY）

TLS 1.3 建立后双方互发 IDENTIFY（§4.2，首帧序列号 = 随机 seq_init），对端 IDENTIFY_RESPONSE 接受后进入在线态；版本不符回 0x01，拒绝回 0x02。

#### Scenario: 双向身份交换

- **WHEN** TLS 通道建立
- **THEN** 双方各自发送 IDENTIFY 并等待对方 IDENTIFY_RESPONSE=0x00，随后进入在线态并开始心跳

#### Scenario: 版本不兼容

- **WHEN** 对端 IDENTIFY 的协议版本与本机不同
- **THEN** 回 IDENTIFY_RESPONSE 0x01 并断链

### Requirement: 保活与断链

在线后每 2 秒互发 HEARTBEAT（收到即回帧并喂狗）；连续 3 个周期无对端心跳判定丢失，断链并按重连策略处理。用户主动断开发送 DISCONNECT (0x00) 后关闭。

#### Scenario: 心跳丢失

- **WHEN** 连续 3 个心跳周期未收到对端 HEARTBEAT
- **THEN** 连接置为重连态并按 §10.1 退避重连（5 次 ×200ms 倍增 ±20% 抖动），期间新 IDENTIFY 使用新 seq_init

#### Scenario: 用户断开

- **WHEN** IPC connect/disconnect 请求断开
- **THEN** 先发 DISCONNECT(0x00) 再关闭，不再自动重连该对端

### Requirement: 延迟测量

在线期间每 2 秒发送 LATENCY_PING（载荷为发送方微秒时钟），收到 PONG 按一半 RTT 记样；采样统计（min/max/avg）经 IPC 暴露。

#### Scenario: PONG 回填

- **WHEN** 收到 LATENCY_PONG
- **THEN** 以 now-T1 的一半记录样本，异常时间戳（未来/超界）丢弃不计

### Requirement: IPC 与状态可见

连接状态以全量快照经 peers.list/peers.changed 暴露（instance/名称/地址/状态/延迟/指纹）；self.info 暴露自身名、实例、指纹、端口。连接/断开经 connect.request/disconnect.request 触发。

#### Scenario: 状态快照

- **WHEN** 任一连接状态变化（连接中/在线/重连/离线）
- **THEN** 推送 peers.changed 全量快照，字段齐全且顺序稳定
