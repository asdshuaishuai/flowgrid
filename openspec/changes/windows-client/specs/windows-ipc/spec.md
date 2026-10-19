# windows-ipc

## ADDED Requirements

### Requirement: 传输与信封

UI 与核心经命名管道 `\\.\pipe\flowgrid-core` 通信，消息为 UTF-8 JSON Lines，每行一个信封：`{"v":1,"type":<string>,"id":<string,可选>,"payload":<object,可选>}`。协议与消息集是客户端内部契约，不属于 wire format。

#### Scenario: 请求-响应关联

- **WHEN** UI 发送带 id 的请求
- **THEN** 核心对应的响应携带相同 id；核心主动推送的事件不携带 id

#### Scenario: 非法与未知消息

- **WHEN** 核心收到无法解析的行、v 不为 1、或未知 type
- **THEN** 对可关联的请求回 `error`（携带原 id），连接不断开

### Requirement: 首批消息集

- `hello` → `hello.ok`：版本互换（payload 含 name/version）
- `status.get` → `status`：mdns 可用性、mdns 错误、设备数、运行时长、核心版本
- `devices.list` → `devices`：全量设备快照
- `device.changed`（核心→UI 推送）：全量设备快照（快照语义，非 diff）

#### Scenario: 空设备表

- **WHEN** 设备表为空时 UI 发送 devices.list
- **THEN** 核心回 `devices` 且 peers 为空数组（不省略字段）

### Requirement: UI 重连语义

#### Scenario: 核心未启动

- **THEN** UI 如实显示未连接并按固定退避自动重试，不崩溃不阻塞界面

#### Scenario: 重连后恢复

- **WHEN** 管道重连成功
- **THEN** UI 重新走 hello + devices.list + status.get 拉全量，不依赖断线前的任何本地状态
