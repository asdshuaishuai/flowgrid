## Context

本轮为 macOS 客户端第一阶段：设计复刻 + Go 协议库 FFI 接线。技术栈与形态约束不在本轮重新决策，沿用既有文档：

- CLAUDE.md《macOS (Swift 6 + SwiftUI)》：XcodeGen + SPM；`FlowGridCore` + `FlowGridUI` 两包 + `FlowGrid` app target；`MenuBarExtra(.window style)` 纯菜单栏；Swift 6 严格并发；`DependencyContainer.swift` 组装；macOS 15+。
- `.workbuddy/memory/2026-10-16.md` 形态硬约束：无主窗口；唯一常驻入口是菜单栏图标；所有界面为按需唤出的浮板、平级、失焦即走。
- AGENTS.md 架构边界：协议库不写平台专属逻辑；传输加密（DTLS/TLS）、HAL、GUI 属客户端；cgo 是唯一 FFI 面，改导出须同步 cgo.h。

## Goals

1. `design/unified/` 8 屏 1:1 SwiftUI 复刻（含 icons/动效/深浅色），可构建、可运行、可目验。
2. protocol 库经 cgo c-archive 首次接入真实客户端：构建、链接、运行时驱动演示数据均走真库。
3. 协议层零改动；传输/HAL/会话层导出留干净接缝。

## Non-Goals

- DTLS 1.3 / ECDH 会话密钥（当前用配对码派生 HMAC 密钥作 MVP 过渡）、mDNS/BLE/WiFi Direct 发现、多设备路由。
- 设备布局编辑器（随多设备回归）、设置持久化落盘、多语言、应用图标、分发签名。

## Decisions

### D1 包结构：三 target 隔离 FFI（`CFlowGrid` / `FlowGridCore` / `FlowGridGo`）

`swift test` 的测试可执行文件会链接模块全部对象（静态库归档不解析符号，但可执行文件必须解析），若 FFI 与纯逻辑同 target，纯逻辑测试也会因 `fg_*` 未定义而链接失败。故：

- `FlowGridCore`：纯 Swift（域模型、服务接缝、演示兜底），可独立 `swift test`；
- `FlowGridGo`：FFI RAII 封装，编译通过、链接由消费方承担；
- `CFlowGrid`：仅承载 cgo 生成头 `flowgrid.h`（提交入库，构建脚本刷新），C target 保持零逻辑。

### D2 链接策略：包级 `linkerSettings` + 工程级 `-lflowgrid`

测试动作会把本地包编成 dynamic 变体 framework（PackageFrameworks），其自身链接必须解析 `fg_*` 符号——仅靠 app 工程 `OTHER_LDFLAGS` 不够。故在 `FlowGridGo` target 声明 `linkerSettings: .unsafeFlags(["-L<Vendor>", "-lflowgrid"])`（路径由 `#filePath` 推算，指向 `mac/Vendor`），app/test 工程同时保留 `-lflowgrid` + `LIBRARY_SEARCH_PATHS`。冒烟验证：c-archive 链接仅需追加 `-framework CoreFoundation`。

XcodeGen 注意：`framework:` 依赖会把 `.a` 按动态 framework 嵌入 app 包导致签坏——不走该路径，用显式链接参数。

### D3 设计系统移植纪律

- 三层 token 逐值搬运：L1 灰阶、L2 语义（light/dark 双值动态色，soft/line 用原三元组 + 分主题 alpha）、L3 平台（macOS accent `#007aff`/`#0a84ff`、SF 字体族）。
- 窗口几何常量（520/300/34/14/28/26/36×20/圆角 6/10/12）与动效参数（220ms enter、150ms 微交互、1800ms toast、900ms 切换遮罩、2s pulse、2s 采样）为硬约束，不得改。
- 图标不做近似替代：实现 SVG 子集解析器（path 全命令集 + rect/circle/ellipse，弧线走端点参数化 + CGPath 变换），逐条移植 app.js 的 57 个图标（24 网格、1.75 描边、currentColor）。
- 设计工具产物（devbar/dock/pager）不移植；深浅色跟随系统（`NSApp.effectiveAppearance`）。

### D4 数值口径：协议优先

设计稿演示值凡与协议实现冲突，以协议为准（沿用 v2.3「界面上的边界值全部取自协议实现」原则）：

| 位置 | 设计稿旧值 | 采用值（协议） |
|---|---|---|
| 延迟分级 | `<8 / <20 / else`（app.js demo） | §9.2 `<2ms` 好 / `2–4ms` 警告 / `>4ms` 差 |
| 告警阈值默认 | 20 ms | 5 ms（`LatencyAlertThresholdMs`） |
| 剪贴板单条上限 | 8 MB（settings 文案） | 63,240 B（§5.3） |
| 加密标注 | XChaCha20-Poly1305（settings 文案） | DTLS 1.3 · AES-256-GCM（§8） |

演示数值挑数保持设计稿绿/橙/灰状态语义；链路命名用 `IntelligentLink`（协议文档 NearLink 改名属独立事项）。

### D5 浮板窗口模型

每屏一个 `NSPanel`（borderless、`.floating`、movableByWindowBackground、key 失焦即 `orderOut`）；同一时刻仅一块并存，开新替旧；定位菜单栏下方居中；入场动效按设计（opacity + translateY(-8) + scale(.97)、220ms `cubic-bezier(.16,1,.3,1)`，在窗口内容层实现）。托盘 = `MenuBarExtra(.window style)`，内容自绘 300px 圆角 + 材质。

### D6 运行时真接入点（FFI 不是只做单测）

- 延迟管线：`GoLatencyFeed`（FlowGridGo）持真 `fg_latency_monitor_new` 句柄，演示传输注入 ping/pong 时间戳，样本（单程 ms/抖动/质量）流向托盘/延迟屏/连接屏；
- 键位映射屏：`keymap.yaml`（随 app 资源）经 `fg_keymap_load_file` 载入真 `KeyMapTable`，行内容由 `Lookup` 驱动；
- 其余 FFI 面（frame/payload/sequence/heartbeat/BLE/RSSI/错误码文案）由 `FlowGridTests` 全量往返覆盖。

### D7 DirectLink 传输（MVP，本轮增量）

- **传输**：Network.framework TCP，端口 24801，长度前缀（2B BE，含 HMAC 尾，§7.2）；
  单连接（v1.1 会话层出帧队列为全局单实例），Manager 拒绝并发入站。
- **握手**：双方各发 IDENTIFY（role=0x03，随机 `seq_init` UInt32 BE）并回 IDENTIFY_RESPONSE；
  首帧序列号必须等于载荷 `seq_init`，不一致则 tracker 重置并记录（§10.3）。IDENTIFY 解析在 Swift
  侧完成（cgo 未导出 unmarshal，集成文档允许）。
- **完整性**：全部帧 HMAC-8；密钥 = SHA-256("FlowGrid-mvp-session|" + 规范化配对码)。
  该派生仅作局域网 MVP 的共享秘密，协议正路 ECDH 随 DTLS 阶段替换。连续 3 帧校验失败断链。
- **会话接线**：100ms tick：轮询出帧队列（sink）→ 打 seq/时间戳/HMAC → TCP；文件 pump 128 块/拍；
  收侧分发 CLIPBOARD/FILE_*/HEARTBEAT/LATENCY_*/HID/IDENTIFY/DISCONNECT。
- **HAL**：CGEventTap（head insert，吞事件）捕获键鼠 → 协议载荷；CGEvent 注入（键/按钮/滚动/移动）。
  依赖辅助功能权限（AXIsProcessTrusted）；仅用户显式「接管」时启用，断开/失败自动关闭。
  macOS 虚拟键码 ↔ HID Usage 映射按 US 布局（其他布局差异留客户端后续处理）。
- **测试**：SPM `swift test`（swift-testing）跑真实 localhost TCP 集成：握手/心跳/延迟/剪贴板双向/
  文件双向（SHA-256 落盘）/错配对码拒连；Xcode 测试 target 因外部卷 runner 阻塞问题移除。

### D8 交互逻辑重排（依据使用反馈）

设计稿除「无主窗口 + 按页组织功能」外均视为参考，按桌面人体工程学重排：
- 设备列表只含真实设备（本机 + 对端）；快速切换 = 接管/交还键鼠（真实语义），
  ⌘1 接管对端、⌘2 切回本机。
- 「连接设备」成为连接主入口（地址 + 配对码），自动发现明确标注未启用；
  移除无真实语义的演示设备、演示传输进度、布局拖拽与冗余预览折叠区（功能页各自承担深挖）。
- 浮板交互补 macOS 惯例：Esc/⌘W 关闭、失焦即走、交通灯悬停字形、初始不抢输入焦点、
  跟随鼠标所在屏幕定位、减弱动态效果适配、悬停提示、文件拖放、真实剪贴板写入、数值滚动过渡。

## Risks / Trade-offs

- **c-archive 平台耦合**：以本机 SDK 构建（min macOS 27 告警）；跨版本分发需目标环境重编——README 记明，分发阶段再统一。
- **SPM dynamic 变体**：`unsafeFlags` 属本地路径包专用的务实选择；若后续引远程依赖需改行 XCFramework 方案。
- **严格并发**：UI/服务 `@MainActor`，FFI 包装 `@unchecked Sendable`（桥内 registry 并发安全、包装类无可变状态）。
- **材质叠加**：MenuBarExtra 系统 chrome 与自绘圆角的偏差在验收时微调。
