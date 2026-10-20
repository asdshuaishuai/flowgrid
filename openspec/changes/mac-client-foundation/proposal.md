## Why

协议层 v1.1 已归档（2026-10-16），kde/、deepin/ 客户端整体移除待重写；`design/unified/` 设计系统（8 屏，v2.3）被定位为「未来客户端 GUI 的唯一设计来源」；CLAUDE.md 已定 macOS 技术栈（Swift 6 + SwiftUI + XcodeGen + SPM，纯菜单栏形态，macOS 15+）。

需要第一次把「设计稿 → 可运行客户端」落地，并首次验证 Go 协议库在 macOS 上的 FFI 通路（cgo c-archive 的构建/链接此前只在 Linux 路线论证过，换机后从未实测）：

- 新客户端按既定约束直接消费 protocol 会话 API，只写 GUI 与传输绑定——本轮先完成 GUI 全量与 FFI 接线，传输与 HAL 留接缝；
- 设计复刻以 `design/unified/` 为唯一来源，逐值（token/几何/动效/文案）1:1；
- 设计稿演示数值与协议实现冲突处（延迟分级、告警阈值、8MB、XChaCha20）一律以协议为准。

## What Changes

- **新增 `mac/` 客户端工程**（仓库首个可运行的 mac 客户端）：
  - XcodeGen `project.yml`（FlowGrid app + FlowGridTests + 两个本地 SPM 包）+ Makefile + `scripts/build-go.sh`；
  - SPM 包 `FlowGridCore`：target 三分（`CFlowGrid` C 暴露层 / `FlowGridCore` 纯 Swift 域模型与服务接缝 / `FlowGridGo` FFI RAII 封装）；
  - SPM 包 `FlowGridUI`：设计系统（三层 token light/dark 逐值移植、排版/几何/阴影/动效）、SVG 图标引擎（24 网格 1.75 描边，app.js 全 57 图标）、组件库、8 屏视图；
  - app target：`MenuBarExtra(.window style)` 托盘（300px）+ 7 块 520px 浮板 NSPanel（borderless / floating / 失焦即关 / 单块并存）、⌘1/2/3、⌘K、⌘,、⌘Q 快捷键、DependencyContainer 注入。
- **FFI 接线（macOS 首次实测通过）**：
  - `go build -buildmode=c-archive` 在 macOS + clang 构建成功，冒烟链接仅需 `-framework CoreFoundation`；
  - 链接策略：SPM target `FlowGridGo` 级 `linkerSettings`（覆盖测试动作的 dynamic 变体链接）+ app/test 级 `-lflowgrid`；
  - 运行时真接入：延迟由真实链路 ping/pong 喂入 Go `LatencyMonitor`（分级/连续超阈值）；键位映射屏由真 `KeyMapTable` 载入 YAML 规则驱动；
  - `FlowGridGoTests` 覆盖帧往返（含 HMAC 篡改拒收）、payload 往返、键位载入与重映射、延迟 monitor、序列号、心跳、BLE 广播等。
- **数值对齐协议**（设计稿演示旧值修正）：延迟分级改 §9.2（<2 / 2–4 / >4 ms）；告警阈值默认 20ms → 5ms（`LatencyAlertThresholdMs`）；剪贴板单条上限 8MB → 63,240 B；加密标注 XChaCha20 → DTLS 1.3 · AES-256-GCM。
- **DirectLink 传输接通（后续增量，upstream 合并后补入）**：
  - TCP 24801 + 长度前缀（§7.2）；IDENTIFY 双向握手（UInt32 seq_init）、序列号防重放（§10.3）、
    心跳 2s 互发互回、延迟 PING/PONG 测量（§9.x）；
  - 会话层真接线：剪贴板推送/接收、文件 OFFER→CHUNK→SHA-256 落盘、自动接收；
  - 帧完整性：全部帧 HMAC-8 校验，连续 3 次失败断链；MVP 会话密钥由配对码 SHA-256 派生
    （DTLS/ECDH 属后续阶段）；
  - 双实例 localhost 实测：双向连接、对端设备行与实时延迟、剪贴板/文件互传；
  - `FlowGridGoTests`：真实 TCP 链路集成测试（握手/心跳/延迟/剪贴板双向/文件双向/错配对码拒连）。
- **HAL（MVP）**：CGEventTap 捕获本机键鼠（Host，返回 nil 吞事件）→ 协议 HID 载荷；
  CGEvent 注入（Target，键/鼠标/滚轮）；辅助功能权限检测与引导；macOS 虚拟键码 ↔ HID Usage
  映射表（US 布局）。
- **交互重排（依据反馈）**：真设备入列（本机 + 对端），切换语义改为「接管/交还键鼠」；
  「连接设备」成为主入口（地址 + 配对码直连，mDNS 明确标注未启用）；移除无真实语义的
  演示设备、演示传输、设备布局拖拽与冗余预览折叠区；浮板补充 Esc/⌘W 关闭、失焦即走、
  交通灯悬停字形、减弱动态效果适配、悬停提示、文件拖放、真实剪贴板写入。
- **不触协议层**：`protocol/`、`design/PROTOCOL.md` 零改动；不新增 cgo 导出（会话层导出留作独立提案）。

## Capabilities

### New Capabilities
- `mac-client-shell`: 纯菜单栏形态（无主窗口；托盘 300px、所有界面为按需唤出的 520px 浮板，唤出即来失焦即走、浮板平级）；`design/unified/` 设计系统的 1:1 SwiftUI 复刻（三层 token、组件层、图标集、动效节奏、light/dark 跟随系统）；8 屏含交互（折叠区/开关/搜索过滤/柱状图/进度/Toast/切换遮罩）；交互按桌面人体工程学重排（真设备、接管语义、连接设备主入口）。
- `mac-protocol-ffi`: cgo c-archive 在 macOS 的构建与链接（含 SPM dynamic 变体下的链接声明）；Swift 侧 RAII 封装（句柄 0 即失败、缓冲 `fg_free_buffer` 只释放一次）；运行时由 Go 真库状态机驱动 UI 数据。
- `mac-directlink-transport`: TCP 长度前缀 + 双向 IDENTIFY 握手 + 序列号/防重放 + 心跳/延迟 + 会话层（剪贴板/文件）真接线 + HAL 捕获/注入（辅助功能权限门控）；HMAC 密钥 MVP 由配对码派生，DTLS/ECDH 待后续。

### Modified Capabilities
- （无）

## Impact

- **新增**：`mac/`（工程/两个 SPM 包/app/测试/构建脚本/Makefile/README）；`.gitignore` 增补 mac 产物规则。
- **GitHub 关系**：不修改 `protocol/`、`design/`、`openspec/` 既有内容；`AGENTS.md` §九目录地图与现状句同步更新（mac 客户端落地、开发机迁至 macOS）。
- **验证**：`cd protocol && go test -count=1 .` 全绿（零波及）；`swift test`（域逻辑 8 + FFI/链路 23，含真实 TCP 集成）；`xcodebuild build` 成功；双实例 localhost 实测连接、剪贴板与文件互传、延迟显示。
- **已知限制（非目标）**：DTLS 1.3/ECDH 会话密钥、mDNS/BLE/WiFi Direct 发现、多设备路由、设备布局编辑器、设置持久化、分发签名——均留接缝，独立提案推进。
- **环境事实**：c-archive 以本机 SDK 构建（min macOS 27）；`protocol/cgo` 的 cgo 子包在 macOS 上可跑（`CGO_ENABLED=1 go test ./cgo` 试跑佐证），归档任务中「Linux + gcc 验证」项仍保留。
