# mac-directlink-transport

## ADDED Requirements

### Requirement: DirectLink TCP 传输

客户端 MUST 以 Network.framework TCP 连接对端（默认端口 24801），记录采用长度前缀封装（2B BE，
覆盖帧体与 8B HMAC 尾，§7.2）；v1.1 单连接模型下 Manager MUST 拒绝并发入站连接。

#### Scenario: 连接与拒绝并发

- **WHEN** 对端监听 24801 且客户端发起连接
- **THEN** TCP 建立后进入握手；已有活动会话时新的入站连接被立即关闭

#### Scenario: 记录分帧

- **WHEN** 收到任意字节流
- **THEN** 按 2B 长度切出完整记录；不足一条时等待后续字节

### Requirement: 握手与序列号

连接建立后双方 MUST 各发 IDENTIFY（含随机 UInt32 `seq_init`）并回 IDENTIFY_RESPONSE；
入站序列号 MUST 以对端 `seq_init` 初始化，首帧必须等于该值；重复/过期帧 MUST 静默丢弃且不回 ERROR（§10.3）。

#### Scenario: 双向握手

- **WHEN** 双方互相收到 IDENTIFY 与 IDENTIFY_RESPONSE 且状态为接受
- **THEN** 进入运行态并记录对端名称/平台；随后启动心跳与延迟采样

#### Scenario: 序列号异常

- **WHEN** 收到 gap/重复/过期序列号的帧
- **THEN** 帧被丢弃，连接保持；gap 时 tracker 前移重同步

### Requirement: 帧完整性与断链

全部帧 MUST 经 HMAC-SHA256 截断 8 字节校验；连续 3 帧校验失败 MUST 断开连接（典型原因是配对码不一致）。
MVP 的 HMAC 密钥由配对码经 SHA-256 派生，仅为局域网过渡，DTLS/ECDH 阶段替换。

#### Scenario: 配对码不一致

- **WHEN** 两端配对码不同
- **THEN** 入站帧持续校验失败并在 3 帧内断开，界面提示校验失败原因

### Requirement: 心跳与延迟测量

双方 MUST 每 2 秒发送 HEARTBEAT 并立即回执；每 2 秒发送 LATENCY_PING，对端原样回 T1；
测量值为单程延迟（半 RTT），按 §9.2 分级（<2 好 / 2–4 警告 / >4 差）。

#### Scenario: 延迟采样

- **WHEN** 收到合法 LATENCY_PONG（时间戳匹配最近 ping）
- **THEN** LatencyMonitor 记录样本并驱动界面数值与分级；非法样本被库拒绝丢弃

#### Scenario: 心跳超时

- **WHEN** 连续 3 次心跳 tick 未收到对端心跳（约 6 秒）
- **THEN** 判定连接丢失并断开，界面回落为未连接

### Requirement: 会话层真接线

剪贴板与文件传输 MUST 经 Go 会话层（ClipboardSession / FileSender / FileReceiver）完成；
发送侧先声明 sink 再轮询出帧队列，接收侧读取即消费；文件 MUST 在 SHA-256 校验通过后才落盘。

#### Scenario: 剪贴板往返

- **WHEN** 一方推送本机剪贴板（≤63,240 B）
- **THEN** 对端经会话层重组后入历史，并在「自动写入本地」开启时写回系统粘贴板

#### Scenario: 文件往返

- **WHEN** 一方拖入文件（1 B – 16 MiB）
- **THEN** 对端自动接受并在校验通过后写入接收目录；失败（哈希不符/超时）不交付

### Requirement: HAL 捕获与注入（MVP）

键鼠接管 MUST 仅在用户显式开启且已授予辅助功能权限时启用；捕获的键盘/鼠标事件 MUST 转换为
协议 HID 载荷并经链路发送，同时不传递给本机（吞事件）；接收端 MUST 通过 CGEvent 注入。
未授权时 MUST 给出引导且不开启捕获。

#### Scenario: 接管开关

- **WHEN** 用户开启「接管对端键鼠」且权限已授予
- **THEN** CGEventTap 启用，本机键鼠事件发送到对端
- **WHEN** 权限未授予
- **THEN** 开关不生效并提示前往系统设置授权

#### Scenario: 注入

- **WHEN** 收到 KEY_DOWN/KEY_UP/MOUSE_MOVE/MOUSE_BTN/SCROLL
- **THEN** 按 macOS 虚拟键码与修饰掩码映射注入对应 CGEvent
