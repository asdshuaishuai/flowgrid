# frame-integrity-hmac (supersedes 2026-06-29 version)

## ADDED/MODIFIED Requirements

### Requirement: 帧完整性验证

每个 HID 帧可选携带 8 字节截断 HMAC-SHA256 完整性尾（原 4 字节决策废止：4 字节截断在 1M pps 下约 71 分钟可在线伪造一帧）。

- `hmac = HMAC-SHA256(session_key, frame_type || seq || timestamp || payload_len || payload)[0:8]`
- 输入为完整 12+N 字节帧（header + payload）
- 接收方以常量时间比较校验；不匹配或缺失 → 丢弃帧 + ERROR (0xFF, code=0x10)
- 已启用 HMAC 的连接上，缺失 HMAC 尾的帧视为验证失败，不得静默放行
- HMAC 连续验证失败 ≥ 3 次 → 立即断开连接

#### Scenario: 启用 HMAC 的连接丢弃无尾帧

- **WHEN** 连接已配置 HMAC key 且收到无 8 字节尾的帧
- **THEN** 解码返回 ErrHmacMismatch，帧不得被处理

#### Scenario: 篡改检测

- **WHEN** 帧任意字节被翻转
- **THEN** 重算 HMAC 与帧尾不一致，返回 ErrHmacMismatch

#### Scenario: 暴破防护

- **WHEN** HMAC 连续校验失败达到 3 次
- **THEN** 调用方必须断开连接（库返回错误，断链动作由连接层执行）
