## 1. OpenSpec

- [x] 1.1 本变更四件套

## 2. 协议层（protocol/）

- [x] 2.1 PROTOCOL.md 升级：§1 两模型表 + §3 智能连接（LAN 私有链发现）+ NearLink→IntelligentLink 术语同步
- [x] 2.2 `protocol/lanid.go`：通告记录 Marshal/Unmarshal + ECDSA-P256 签名/验签
- [x] 2.3 `protocol/lanid.go`：Announcer/Listener 纯逻辑状态机（Announce 产生字节 / Feed 喂字节）
- [x] 2.4 `protocol/lanid.go`：私有链信任日志（哈希链式签名记录 + 追加/查询/校验）
- [x] 2.5 `protocol/link.go`：两模型选择纯逻辑（三策略）
- [x] 2.6 cgo 导出：`fg_lanid_*` / `fg_link_plan`（21 个导出，cgo.h 已重新生成）
- [x] 2.7 表驱动单测：记录往返、验签（正确/篡改/错密钥）、链式校验、监听聚合/淘汰、选择三策略

## 3. mac 客户端

- [x] 3.1 DirectLinkManager 接入智能连接（start/stop + 对端列表 surfaced + 身份持久化）
- [x] 3.2 UDP 多播薄适配器（`LanIDLink.swift`：BSD socket → 喂协议层）
- [x] 3.3 `DiscoveredPeer` 扩展（source/fingerprint/trusted）；首接触指纹确认 UI 为后续增量
- [x] 3.4 连接屏：智能连接对端列表（经 discoveredPeers 合并展示）
- [x] 3.5 验证：swift build/test（46+11 例）+ make build 全绿

## 4. Windows / Linux 适配器

- [x] 4.1 Windows fgcore：UDP 多播适配器（`internal/lanid/lanid.go`，Go 直引协议层）——本机编译 + vet 验证
- [x] 4.2 deepin：RAII 封装（`protocolapi.h/cpp`）+ UDP 多播适配器（`lanidlink.h/cpp`，QUdpSocket）
- [ ] 4.3 各自开发机验证（Windows 模块整包构建 / deepin DTK 构建 + CMakeLists 加入新文件）

## 5. 验证

- [x] 5.1 `cd protocol && go test -count=1 . && go vet .` 全绿
- [x] 5.2 `cd protocol && CGO_ENABLED=1 go test ./cgo` 全绿
- [ ] 5.3 三平台互通矩阵（人工，需三台机器）——见下

## 互通验证矩阵（Phase 4，需三台机器）

### 智能连接（LAN 私有链）
1. 三台机器同时启动（mac / Windows fgcore / deepin），各自多播通告（239.255.42.99:24801，2s 周期）
2. 互相发现：连接屏出现对端（source=智能连接，含指纹）
3. 首接触：展示指纹 → 人工确认 → 信任链追加
4. 已信任对端：自动识别 → 可发起 TCP 连接 → IDENTIFY 握手 → 剪贴板/文件/键鼠全链路

### 直连（mDNS）对照
- mDNS 可用时连接屏同时显示 mDNS 对端（source=mDNS），双模型并存

### 无蓝牙场景
- 无 BLE 硬件的机器：智能连接全功能（纯 UDP 多播，不依赖射频）
- mDNS 不可用时：智能连接兜底

### 预期
- 三平台互通：mac ↔ Windows ↔ deepin 均能发现、信任、连接（协议层 + cgo 导出保证线格式一致）
- 无蓝牙不全功能丧失：DirectLink + 智能连接双路径
