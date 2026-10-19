## 1. OpenSpec

- [x] 1.1 本变更四件套；PROTOCOL-ISSUES 登记 P2（TXT encryption 缺 tls13 值）

## 2. 连接引擎（internal/link）

- [x] 2.1 identity.go：自签证书生成/加载 + 指纹
- [x] 2.2 trust.go：信任库持久化（JSON）
- [x] 2.3 session.go：每对端会话（读写 goroutine、IDENTIFY、心跳、延迟、出站队列）
- [x] 2.4 engine.go：监听/拨号/确认门/重连/状态注册表/ resolver 解耦
- [x] 2.5 单测：信任库、IDENTIFY 状态推进、会话在 net.Pipe/tls 环回上的握手

## 3. 自通告与 IPC

- [x] 3.1 fgcore 启动 MdnsAdvertisor + Core 自身过滤
- [x] 3.2 IPC：self.info / peers.* / connect.* / pair.* 消息集 + peers.changed/pair.pending 推送
- [x] 3.3 main.go 装配（-name/-port/-data/-auto-accept/-pipe）

## 4. UI 接线

- [x] 4.1 PeerLink：peers/peers.changed/pair.pending 消费 + connect/disconnect 发送
- [x] 4.2 连接屏：设备卡连接/断开按钮 + 链接状态徽标；配对屏：pending 确认/拒绝

## 5. 验证

- [x] 5.1 双进程环回冒烟 smoke-link.ps1（发现→连接→在线→断开清理）
- [x] 5.2 go test ./... 全绿 + vet + gofmt；dotnet build 零警告
- [x] 5.3 协议层零改动核查（git diff protocol/ 仅存量 mdns_test.go）
