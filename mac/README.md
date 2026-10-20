# FlowGrid · macOS 客户端

纯菜单栏应用（无主窗口），界面按需唤出为浮板：托盘浮层 `300px` + 7 块 `520px` 浮板
（NSPanel：失焦即关、Esc 关闭、单块并存、内容自适应高度）。视觉与组件体系来自
`design/unified/`（三层 token / 55 图标 / 动效节奏），交互逻辑按桌面人体工程学重排。

## 功能现状

| 能力 | 状态 |
|---|---|
| DirectLink 传输（TCP 24801，长度前缀 + 帧级 HMAC） | ✅ 已接通（双实例实测） |
| IDENTIFY 双向握手 / 序列号防重放 / 心跳 / 延迟测量 | ✅ 已接通 |
| 自动重连（5 次指数退避 ±20% 抖动；认证失败不重试） | ✅ |
| mDNS 发现（广告 `_flowgrid._tcp` + 浏览 + 按 service 连接） | ✅ 已实现（受本地网络权限约束） |
| 剪贴板同步（真会话层：分段/重组/5s 超时） | ✅ 已接通（推送与接收） |
| 文件传输（真会话层：249 B 分块 / SHA-256 / 自动接收落盘） | ✅ 已接通（拖放或选择发送） |
| HAL：CGEventTap 捕获（Host）+ CGEvent 注入（Target） | ✅ 已实现（需辅助功能权限） |
| 键位映射屏（真 KeyMapTable 载入 YAML 驱动） | ✅ |
| 设置持久化（`~/Library/Application Support/FlowGrid/settings.yaml`，2s 自动保存） | ✅ |
| 开机自启（SMAppService 真实注册；开发签名失败时回退提示） | ✅ |
| DTLS 1.3 / ECDH 会话密钥、mDNS/BLE 发现、设置持久化、签名分发 | ⏳ 后续阶段 |

**MVP 安全说明**：当前 DirectLink 为明文 TCP，HMAC 密钥由「配对码」经 SHA-256 派生
（`SessionKey.derive`）。协议正路是 DTLS/配对阶段的 ECDH（§4.3），届时替换密钥来源；
配对码仅作局域网 MVP 的临时共享秘密。

## 环境（已验证）

macOS 27 · Xcode（SDK 27）· Go 1.25.10 · XcodeGen 2.45.4。

## 构建与运行

```bash
cd mac
make bridge     # protocol/cgo → c-archive（mac/Vendor/libflowgrid.a + 刷新 C 头，含冒烟链接）
make generate   # XcodeGen 生成 FlowGrid.xcodeproj（生成物不入库）
make build      # xcodebuild（preBuildScript 会按需自动构建 c-archive）
make run        # 构建并启动
make test       # 全部单元 + 真实 TCP 集成测试（swift test：23 + 8 用例）
```

App 产物：`~/Library/Caches/FlowGrid/DerivedData/Build/Products/Debug/FlowGrid.app`
（派生数据放内置盘的原因见「已知环境坑」。可用 `DERIVED=...` 覆盖。）

## 两台设备连接

1. 两台机器都启动 FlowGrid（默认监听 `24801`）。
2. 托盘 →「连接设备」，填写对端地址（如 `192.168.1.50:24801`）与**对端显示的本机配对码**；
   两端默认都是 `8421`，可各自「刷新」后互相告知。
3. 连接成功后，「设备与连接」里出现对端行与实时延迟；托盘「快速切换」= 本机 ⇄ 对端。
4. 接管键鼠前，在「设备与连接 → 接管对端键鼠」开启开关，并按提示到
   **系统设置 → 隐私与安全性 → 辅助功能** 授予权限（首次需手动开启）。
   开启后本机按键/鼠标事件被捕获并发送到对端（本机指针不再移动），关闭即回到本机。

开发辅助（直接以可执行文件启动才带环境变量）：

```bash
APP=~/Library/Caches/FlowGrid/DerivedData/Build/Products/Debug/FlowGrid.app/Contents/MacOS/FlowGrid
FG_OPEN_PANEL=connections "$APP"                 # 启动即打开指定浮板
FG_CONNECT="127.0.0.1:24801|8421" "$APP"         # 启动即连接对端（本机双实例联调）
```

## 目录导览

```
project.yml / Makefile / scripts/build-go.sh   XcodeGen 工程、构建入口、c-archive 脚本
FlowGridCore/          SPM 包（三 target + 两个测试 target）
  CFlowGrid/           cgo 生成头（include/flowgrid.h，构建时刷新，提交入库）+ 占位 C 源
  FlowGridCore/        纯 Swift：域模型 / 样例数据 / LatencyStore / 协议常量 / 连接类型契约
  FlowGridGo/          FFI 封装（RAII）+ 传输 + HAL：
                       FrameCodec / Payloads / KeyMap / LatencyMonitor / SequenceTracker /
                       HeartbeatTracker / Protocol / Session / ClipboardSession / FileSender /
                       FileReceiver / GoKeyMapRows
                       Transport/  TCPFraming / GoIdentify / PeerSession（握手/序列/心跳/延迟/分发）/
                                   DirectLinkManager（监听 + 主动连接）
                       HAL/        KeyCodeTable（macOS VK ↔ HID）/ InputInjector / InputCaptureTap
  Tests/               FlowGridCoreTests（域逻辑）+ FlowGridGoTests（FFI 23 用例，含真实 TCP 链路）
FlowGridUI/            SPM 包
  DesignSystem/        FGColor（三层令牌 light/dark）/ FGType / FGGeometry / FGMotion /
                       FGShadow / FGViewModifiers（帮助标签、数值过渡）
  Icons/               SVG 子集解析器 + 55 图标注册表（源自设计稿 app.js）
  Components/          ~28 个组件（按钮/开关/输入/下拉/分段/折叠区/列表/柱图/传输行/拖放区…）
  Overlays/            Toast（1800ms）/ 切换设备遮罩（900ms）
  Chrome/              浮板外壳（标题栏交通灯悬停字形 / 页脚）
  Screens/             8 屏 + AppModel（@Observable 全局状态与行为）
FlowGrid/              app target：MenuBarExtra / PanelController / DependencyContainer
```

## 协议实现要点

- **握手**：连接建立后双方各发 IDENTIFY（带各自随机 `seq_init`，UInt32 BE），
  收到即入站序列号初始化并以 IDENTIFY_RESPONSE 应答；双方收齐后进入运行态（§4.2）。
- **序列号**：IDENTIFY 首帧必须等于载荷中的 `seq_init`；gap 前移重同步、重复/过期帧静默丢弃、
  不回 ERROR（§10.3）。
- **完整性**：全部帧走 `EncodeBytesWithHMAC` / `DecodeBytesWithHMAC`（8 字节截断）；
  连续 3 帧校验失败即断链（配对码不一致的典型表现）。
- **会话层**：发送侧 `GoSession.setSink(true)` 后由 100ms tick 轮询出帧队列、打序列号/HMAC、
  经 TCP 长度前缀发送；接收侧解码后按 FrameType 分发到 ClipboardSession / FileReceiver /
  FileSender / 心跳 / 延迟 / HAL 注入。
- **心跳与延迟**：双方每 2s 各自发送 HEARTBEAT（收帧仅喂狗、不回帧——双向角色下
  「收到即回」会形成无限回显风暴，见 PROTOCOL-ISSUES P4）；每 2s 发 LATENCY_PING，
  对端原样回 T1，Host 侧 `LatencyMonitor` 计算单程延迟与 §9.2 分级。
- **剪贴板**：推送取本机 `NSPasteboard` 文本（≤63,240 B）；接收按 MIME 入历史，
  「接收后自动写入本地」开启时才写回粘贴板。
- **文件**：拖放到发送区或点选文件 → OFFER → 自动接收 → SHA-256 校验通过后落盘
  `~/Downloads/FlowGrid/`（重名自动改名 `name (2).ext`）；单方向同时仅一路（库内保证）。
- **重连**：意外断开（对端掉线/网络中断）按 §10.1 自动重试 5 次（200ms 起指数退避 ±20%，上限 5s）；
  配对码错误/版本不兼容/对端拒绝属不可恢复错误，不重试并即时提示。
- **发现（mDNS，§3.2）**：随监听广告 `_flowgrid._tcp`（TXT：version/platform/name/caps/port/encryption），
  同时浏览局域网并把候选设备呈现在「连接设备」屏；点「连接」按 Bonjour 端点直连（配对码默认用本机码）。
  注意：macOS 的本地网络隐私会静默拦截未授权应用的浏览 —— 首次使用请在
  「系统设置 → 隐私与安全性 → 本地网络」中允许 FlowGrid；未授权时仍可用 IP 地址直连。

## FFI 说明

- `scripts/build-go.sh` 在 `protocol/cgo` 执行 `go build -buildmode=c-archive`，产物进 `mac/Vendor/`（gitignore），并把生成头复制为 `CFlowGrid/include/flowgrid.h`（提交入库）。冒烟检查用 C 程序调用 `fg_frame_encode` 验证链接参数（macOS 仅需 `-framework CoreFoundation`）。
- 链接策略：`FlowGridGo` target 自带 `linkerSettings`（覆盖测试动作的 dynamic 变体链接），app 工程另配 `-lflowgrid` + `LIBRARY_SEARCH_PATHS`。不要用 XcodeGen 的 `framework:` 依赖指向 `.a`（会被当动态框架嵌入、签名失败）。
- 句柄从 1 起、0 即失败；返回缓冲用 `fg_free_buffer` 只释放一次。
- **改 `protocol/cgo` 导出后必须同步 `cgo.h`**（AGENTS.md 约定：Linux 上重新生成）；本机（macOS + 同版本 Go）生成结果与仓库内 cgo.h 逐字节一致，可作预检。
- 会话层为「Go 缓存、C 轮询」：发送侧先 `fg_session_set_sink(1)`，`fg_session_poll_out` 逐帧取「类型+载荷」，序列号/时间戳/HMAC 由客户端用帧级 API 自行附加；接收侧 `fg_clipboard_take_pending` / `fg_file_receiver_take_offer|take_complete` 读取即消费。
- IDENTIFY 载荷的解析在 Swift 侧完成（cgo 未导出 unmarshal；集成文档 §8 允许客户端解析这几个字段）。

## 交互细节（macOS 惯例）

- 浮板：Esc 关闭、失焦即关、`⌘W` 关闭、锚定鼠标所在屏幕的菜单栏下方；初始不抢输入焦点；
  交通灯悬停显示 × / − / + 字形（仅关闭可点）。
- 动效遵循系统「减弱动态效果」：开启时禁用位移/缩放/脉冲，仅保留淡入。
- 悬停提示（tooltip）覆盖图标按钮；延迟读数与胶囊使用数值滚动过渡。
- 拖放区支持直接把文件拖入发送（高亮 + 「松开以发送」文案）。

## 快捷键

app 活跃时：`⌘1` 接管对端、`⌘2` 切回本机、`⌘K` 键位映射、`⌘,` 设置、`⌘W` 关闭浮板、`⌘Q` 退出。
全局热键需要辅助功能权限，后续阶段接入。

## 已知环境坑（macOS 27）

- **测试 runner + 外置卷**：沙箱化的 xctest 进程打开外置卷上的测试包会阻塞在 `open`（TCC 已放行也卡）。测试统一走 SPM `swift test`（派生数据在内置盘），Xcode 测试 target 已移除。
- **截图**：未授予屏幕录制权限时全屏 `screencapture` 只返回壁纸；窗口级 `screencapture -l <windowID>` 可用（本仓库验证据此完成）。
- 托盘浮层是系统 MenuBarExtra popover，无法程序化唤出，需人工点击菜单栏图标目验。
- c-archive 以本机 SDK 构建（链接时会出现「built for newer macOS」告警）；跨版本分发需在目标环境重编。

## 下一步（独立提案）

DTLS 1.3 + ECDH 会话密钥（替换配对码派生）、mDNS/BLE 发现与多设备路由、
设备布局编辑器（随多设备）、签名与公证分发。
