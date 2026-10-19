# windows-clipboard-history

## ADDED Requirements

### Requirement: 历史存储

fgcore 维护文本剪贴板历史（环形，默认 50 条、可调 50/100/200），记录全文、来源（对端名或「本机」）、方向、字节大小与时间。入史点：本地剪贴板广播与对端重组完成。

#### Scenario: 入史与淘汰

- **WHEN** 历史达到容量后新增一条
- **THEN** 最旧一条被淘汰，新条目在列表首位

#### Scenario: 清空

- **WHEN** IPC clipboard.clear
- **THEN** 历史清空且 UI 列表刷新为空态

### Requirement: 读取与写回

`clipboard.history` 返回元数据列表（不含全文）；`clipboard.entry {index}` 返回该条全文；「写入本地」= 取全文后写入 OS 剪贴板（进入回声抑制，不再次广播）。

#### Scenario: 写入本地

- **WHEN** UI 对某条历史执行写入本地
- **THEN** OS 剪贴板变为该条全文，且不产生新广播/新历史条目

### Requirement: 跨端同步开关

`clipboard.enable` 运行时切换跨端同步：关闭时本地变更不广播、入站不写回 OS 剪贴板；历史与读回不受影响。

#### Scenario: 关闭同步

- **WHEN** 跨端同步关闭后本机剪贴板变化
- **THEN** 对端收不到，本机历史不新增，开关状态经推送可见
