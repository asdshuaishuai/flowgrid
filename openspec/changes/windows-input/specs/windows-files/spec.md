# windows-files

## ADDED Requirements

### Requirement: 文件收发

每个在线链接一对 FileSender/FileReceiver（§7.4：每方向同时一个传输）。入站 OFFER 自动接受，SHA-256 校验通过后写入 `<data>/downloads/`（重名追加序号）；出站经 IPC `file.send {instance,path}` 发起（>16MiB 拒绝）。Pump 由 50ms ticker 驱动。

#### Scenario: 端到端文件落盘

- **WHEN** A 对在线的 B 执行 file.send 且 B 自动接受
- **THEN** B 的 downloads 目录出现同名文件，内容与源文件逐字节一致（SHA-256 校验由会话保证）

#### Scenario: 超限拒绝

- **WHEN** file.send 的文件大于 16MiB
- **THEN** 本地直接拒绝并回错，不发起 OFFER

### Requirement: 传输进度可见

双向传输进度经 `file.progress` 推送（方向、文件名、已传/总量、完成/失败），`file.transfers` 可拉全量；文件屏列表实时刷新。

#### Scenario: 进度推送

- **WHEN** Pump 发出数据块或接收侧收齐
- **THEN** 双侧 UI 收到 file.progress，完成后标记 done
