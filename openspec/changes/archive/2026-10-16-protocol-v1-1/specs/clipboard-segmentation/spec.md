# clipboard-segmentation

## ADDED Requirements

### Requirement: CLIPBOARD 载荷内嵌分段

CLIPBOARD (0x07) 载荷布局：MIME 长度(1) + MIME(M) + Total Size(4, UInt32 BE) + Frag Idx(1) + Frag Tot(1) + Data(N)。

- 单帧 Data 上限 = 255 - (7 + M) 字节；总上限 = 255 × (248 - M)（空 MIME 全局上限 63240 字节）
- 超过单帧上限的剪贴板必须分段：每段一个独立 CLIPBOARD 帧，按 Sequence 顺序发送
- 接收方按 Idx 收齐 Tot 段后按序拼接，拼接总长必须等于 Total Size

#### Scenario: 大剪贴板分段往返

- **WHEN** 发送 60000 字节剪贴板（text/plain）
- **THEN** 分段数 = ceil(60000/238)，每段独立编解码后拼接与原数据逐字节一致

#### Scenario: 接收侧防御

- **WHEN** 收到与进行中传输 FragTot/MIME/TotalSize 不符的非零段
- **THEN** 返回 ErrInvalidPayload，不得索引组缓冲
- **WHEN** 收到 FragIdx=0 的新传输
- **THEN** 未完成的旧传输被替换
- **WHEN** 5 秒未收齐
- **THEN** 丢弃整份（惰性求值）

### Requirement: 同刻单传输

同一连接同一时刻只允许一个剪贴板传输（设备配对为 1:1）。
