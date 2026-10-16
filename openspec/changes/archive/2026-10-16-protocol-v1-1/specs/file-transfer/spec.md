# file-transfer

## ADDED Requirements

### Requirement: 文件传输协议

四种帧：FILE_OFFER (0x21) / FILE_ACK (0x22) / FILE_CHUNK (0x23) / FILE_ABORT (0x24)。

- FILE_OFFER：TransferID(2) + Size(8, 1..16MiB) + SHA-256(32) + NameLen(1) + Name(M，仅文件名，禁路径分隔符)
- FILE_ACK：TransferID(2) + Status(1)：0x00 接受 / 0x01 忙 / 0x02 用户拒绝 / 0x03 超限 / 0x04 哈希不符 / 0x05 完成
- FILE_CHUNK：TransferID(2) + ChunkIdx(4, UInt32 BE) + Data(≤249)
- FILE_ABORT：TransferID(2) + Reason(1)

#### Scenario: 接受后完整传输

- **WHEN** 接收方对 FILE_OFFER 回 ACK accepted，发送方流式发送全部 CHUNK
- **THEN** 接收方收齐后校验 SHA-256，通过则回 ACK complete 并交付数据

#### Scenario: 哈希不符

- **WHEN** 拼接结果与 OFFER 中的 SHA-256 不符
- **THEN** 回 ACK 0x04，数据不交付

#### Scenario: 接收方失联恢复

- **WHEN** active 传输超过 FileIdleTimeout(10s) 无进展
- **THEN** 接收方在下一个 OFFER 到达时将该传输以 transferTimeout 失败并接受新传输，不得永久回 Busy

#### Scenario: 传输限流

- **WHEN** 单方向已有 pending/active 传输
- **THEN** 新 OFFER 以 ACK 0x01 拒绝

### Requirement: 安全边界

- 数据校验通过前不得写入目标路径（内存/临时区）
- 文件名不得包含路径分隔符，且不等于 "." / ".."
