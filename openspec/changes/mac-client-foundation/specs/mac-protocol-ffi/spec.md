# mac-protocol-ffi

## ADDED Requirements

### Requirement: c-archive 构建与链接（macOS）

Go 协议库 MUST 经 cgo 导出 C ABI（c-archive）接入 Swift 客户端；macOS 上构建、链接全链路可用。

#### Scenario: 构建产物

- **WHEN** 运行 `mac/scripts/build-go.sh`
- **THEN** 产出 `mac/Vendor/libflowgrid.a` 与刷新后的 `CFlowGrid/include/flowgrid.h`（含全部导出，句柄/缓冲语义与 cgo.h 一致），且冒烟链接（C 程序调用 `fg_frame_encode`）通过

#### Scenario: 测试动作链接

- **WHEN** `xcodebuild test`（本地包会编出 dynamic 变体 framework）
- **THEN** `fg_*` 符号经 `FlowGridGo` target 的 linkerSettings 解析成功，测试进程可调用 Go 函数

### Requirement: Swift RAII 封装

对 cgo 桥的封装 MUST 遵守仓库约定：句柄从 1 起、0 即失败；返回缓冲经 `fg_free_buffer` 只释放一次；字符串结果按桥约定释放。

#### Scenario: 句柄失败

- **WHEN** 以不存在的路径载入 keymap
- **THEN** 构造抛错（不产生句柄占用），后续调用不可达

#### Scenario: 生命周期

- **WHEN** `GoKeyMap` / `GoLatencyMonitor` / `GoSequenceTracker` / `GoHeartbeatTracker` 实例释放
- **THEN** 对应 `fg_*_free` 恰好调用一次；返回的 Data 缓冲已在读取后立即释放

### Requirement: 帧完整性往返

帧编解码 MUST 经真库执行，完整性语义与协议一致（HMAC 8 字节截断、尾部缺失/不匹配拒绝）。

#### Scenario: 帧往返

- **WHEN** 编码任意帧（类型/序列/时间戳/载荷）再解码
- **THEN** 字段与载荷逐字节一致

#### Scenario: HMAC 拒收

- **WHEN** 带 key 解码一段被篡改过任意字节的帧，或以错误 key 解码
- **THEN** 解码失败（`ErrHmacMismatch` 语义），不返回帧

### Requirement: 运行时真接入

不只单测：UI 演示数据 MUST 由 Go 库状态机真实驱动。

#### Scenario: 延迟管线

- **WHEN** 演示传输按 2s 周期注入 ping/pong（含合成 RTT）
- **THEN** 托盘与延迟屏/连接屏的延迟数值、质量分级来自真 `LatencyMonitor` 句柄样本（单程 ms = 半 RTT），非法 PONG 被库拒绝

#### Scenario: 键位映射屏

- **WHEN** 打开键位映射屏
- **THEN** 展示内容由示例 `keymap.yaml` 经 `fg_keymap_load_file` 载入的真规则表（`Lookup` 结果）驱动

#### Scenario: 序列号与心跳

- **WHEN** 测试注入序列 gap/重复帧
- **THEN** `SequenceTracker` 按协议拒绝（丢帧不回 ERROR 语义）；心跳 3 次缺失判定连接丢失
