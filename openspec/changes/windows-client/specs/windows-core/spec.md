# windows-core

## ADDED Requirements

### Requirement: fgcore 生命周期

fgcore 启动即创建命名管道并开始 mDNS 浏览，随后进入发现轮询与过期清扫循环；收到 SIGINT/SIGTERM 时停止 IPC、停 mDNS、释放资源后退出。

#### Scenario: 组播不可用降级

- **WHEN** MdnsBrowser.Start 返回错误（无组播网段/端口被占）
- **THEN** 进程不退出，IPC 照常服务，status 中 mdns=false 并附错误原因

### Requirement: 设备表与变更广播

设备表以 `DiscoveredPeer.Key` 合并同设备多次通告；发现轮询收新/更新、过期清扫（60s 未再现）淘汰，两者产生变化时向所有已连接的 UI 广播 `device.changed` 全量快照。

#### Scenario: 同设备重复通告

- **WHEN** 同一实例+地址再次通告
- **THEN** 设备表仍为一条，产生一次变更广播

#### Scenario: 过期淘汰

- **WHEN** 设备超过 60 秒未再通告
- **THEN** 从设备表移除并广播变更

#### Scenario: 快照稳定排序

- **WHEN** UI 请求设备快照
- **THEN** 列表按名称稳定排序，多次请求顺序一致
