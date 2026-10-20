## 0. 构建基建（macOS 首次打通）

- [x] 0.1 `mac/scripts/build-go.sh`：`go build -buildmode=c-archive`（macOS + clang 实测）；产物 `mac/Vendor/libflowgrid.a` + 刷新 `CFlowGrid/include/flowgrid.h`（84 导出，含会话层与既有缺失符号）；`--if-needed` 增量、冒烟链接检查
- [x] 0.2 `mac/project.yml`（XcodeGen）：app + tests + 本地包 + `-lflowgrid`/搜索路径；preBuildScript 按需构建 c-archive；ad-hoc 签名；资源打包（keymap.yaml）
- [x] 0.3 `mac/Makefile`（bridge/generate/build/test/run/clean，派生数据在内置盘）；`.gitignore` 增补（`mac/Vendor/`、`mac/*.xcodeproj/`、`.build/`、`.swiftpm/`）
- [x] 0.4 首轮链路验证：`swift build` 三 target 通过；`xcodegen generate` + `xcodebuild build` 成功；FFI 冒烟往返测试通过

## 1. OpenSpec 建档

- [x] 1.1 proposal.md / design.md / tasks.md / specs（`mac-client-shell`、`mac-protocol-ffi`），`openspec validate --strict` 通过

## 2. FFI 层（FlowGridGo）

- [x] 2.1 `GoFrameCodec`（encode/decode ±HMAC、validate 抛 §12 错误码：0x11 类型 / 0x12 长度）
- [x] 2.2 `GoPayloads`（keydown/keyup/mousemove/mousebtn/scroll/latency/disconnect/error/clipboard 构造与解包）
- [x] 2.3 `GoKeyMap`（载入/查询/RAII）、`GoLatencyMonitor`、`GoSequenceTracker`、`GoHeartbeatTracker`、`GoProtocol`（重映射/RSSI/BLE/错误码文案）
- [x] 2.4 链接策略：包级 `linkerSettings` 覆盖 dynamic 变体 + 工程级 `-lflowgrid`（XcodeGen `framework:` 会误嵌 `.a` 导致签坏，已规避）
- [x] 2.5 会话层导出封装（上游协议合并后补齐）：`GoSession`（sink 门控 + poll_out）、`GoClipboardSession`（send/handle/take_pending）、`GoFileSender`（offer/pump/poll）、`GoFileReceiver`（take_offer/accept/reject/take_complete）、`GoProtocolError`
- [x] 2.6 `FlowGridTests`：会话往返全绿（剪贴板 60KB 分段往返 / 丢包二轮补发 / 文件传输 SHA-256 交付 / 无 sink 报 0x05 / validate 0x11）+ 帧冒烟；测试无宿主运行
- [x] 2.7 控制类 FFI 往返（延迟 monitor/统计/连续 bad、序列号语义与回绕、心跳计数、BLE 广播、RSSI、错误码文案）——迁至 `FlowGridGoTests`（swift-testing）

## 3. 设计系统与图标（FlowGridUI）

- [x] 3.1 Tokens：三层逐值（light/dark 动态色、soft/line 分主题 alpha、accent 族含 color-mix 推导、阴影五档、几何常量、字距 em→pt）
- [x] 3.2 图标引擎：SVG 子集解析器（path 全命令 + rect/circle/ellipse，椭圆弧端点参数化→三次贝塞尔）+ app.js 全 55 图标注册表 + 8 用例单测
- [x] 3.3 组件库：按钮/图标按钮/徽标/状态胶囊/状态点/开关/输入/数字输入/下拉/分段/按键帽/设备行/延迟胶囊/折叠区/列表/键位对/柱状图/剪贴板行/预览卡/进度条/传输行/拖放区/布局网格/步骤条/配对码/Toast/切换遮罩/浮板外壳与页脚
- [x] 3.4 动效与材质：enter/micro/toast/overlay/pulse 参数；浮板入场（窗口级 220ms 专用曲线）、系统窗影替代 CSS 阴影、标题栏/页脚层级

## 4. 域模型与服务（FlowGridCore）

- [x] 4.1 域模型：Device/ClipboardItem/TransferItem/KeyMappingRow/DeviceLayout/PairingCandidate/AppSettings + LatencyTier（数值口径按 design.md D4）
- [x] 4.2 演示数据（DemoData，文案与 8 屏 1:1；分段数按 §5.3 公式推算）+ LatencyStore（@Observable 集中状态）
- [x] 4.3 `GoLatencyDriver`：真 Go LatencyMonitor 驱动的延迟管线（预热 12 采样 + 2s 周期）；`GoKeyMapRows`：示例 YAML 经真 KeyMapTable 生成键位行
- [x] 4.4 核心单测（9 用例：分级边界、分段公式、布局交换、存储图表、文案、设置默认值、演示数据完整性）

## 5. 8 屏（FlowGridUI）

- [x] 5.1 托盘（300px：品牌头/延迟胶囊/当前设备卡/快速切换×3/连接/数据与同步/快捷开关×4/⌘K/⌘,/退出）
- [x] 5.2 设备与连接（4 设备行 + 5 折叠区：键位/延迟柱图/剪贴板/传输/布局拖拽）
- [x] 5.3 添加设备（步骤条/配对码 4 格刷新/候选设备（RSSI 经协议分级）/手动添加折叠）
- [x] 5.4 网络延迟（大读数/60s 柱图/分设备列表/告警阈值折叠 5ms·3 次·自动切换）
- [x] 5.5 键位映射（真 YAML 驱动：全局网格/应用覆盖/冲突横幅/高级折叠）
- [x] 5.6 剪贴板（预览卡/分段过滤/搜索/历史 5 行含 176 段与超限/设置 4 开关 + 协议边界说明）
- [x] 5.7 文件传输（进行中 2 行实时进度/ETA、拖放区、记录 6 行覆盖全部 ACK、接收设置）
- [x] 5.8 设置（连接/输入/剪贴板/安全四组，数值全对齐协议，草稿式取消/保存）
- [x] 5.9 AppModel 可观察状态 + 演示定时器（2s 采样、700ms 进度、校验态→完成归档）

## 6. App 组装

- [x] 6.1 `FlowGridApp`：MenuBarExtra(.window) + 设计稿图标渲染的菜单栏模板图标；⌘1/2/3、⌘K、⌘,、⌘Q（app 活跃时）
- [x] 6.2 `PanelController`：520 浮板 NSPanel（borderless/可 key/floating/失焦即关/单块并存/内容自适应高度/入场动效）；`FG_OPEN_PANEL` 开发辅助
- [x] 6.3 `DependencyContainer`：Go 延迟驱动 + 键位 YAML 载入（用户目录优先，兜底演示行）+ RSSI 分级注入；Info.plist（LSUIElement）

## 7. DirectLink 传输与 HAL（本轮增量）

- [x] 7.1 TCP 长度前缀（§7.2）+ DirectLinkManager（监听 24801 / 主动连接 / 单连接约束）
- [x] 7.2 双向 IDENTIFY 握手（UInt32 seq_init / 首帧校验 / RESPONSE 应答）+ 序列号防重放分发
- [x] 7.3 会话接线：剪贴板推送/接收、文件 OFFER→CHUNK→SHA-256 落盘、自动接收、心跳互发互回、延迟 PING/PONG
- [x] 7.4 HAL：KeyCodeTable（macOS VK ↔ HID，US 布局）、CGEventTap 捕获（吞事件）、CGEvent 注入、辅助功能权限门控与引导
- [x] 7.5 集成测试（`FlowGridGoTests`，swift-testing）：localhost 真实 TCP —— 握手/心跳/延迟/剪贴板双向/文件双向/错配对码拒连；Xcode 测试 target 移除（外部卷 runner 阻塞）
- [x] 7.6 双实例联调：实例 B 通过 `FG_CONNECT` 连接实例 A，双端「已连接」对端行与实时延迟（截图验证）

## 8. 交互重排（依据反馈）

- [x] 8.1 连接屏：真设备入列（本机 + 对端）+ 接管开关 + 快捷入口，去掉演示设备与冗余折叠区
- [x] 8.2 连接设备屏：地址 + 配对码直连、本机配对码（复制/刷新）、mDNS 未启用说明
- [x] 8.3 托盘：当前设备 = 接收输入方；快速切换 = 接管/交还（⌘1/⌘2）；延迟/状态真实
- [x] 8.4 文件屏：拖放即发 + 空态；剪贴板屏：真实粘贴板读取/写入；移除演示进度与假记录
- [x] 8.5 macOS 交互细节：Esc/⌘W、初始不抢焦点、交通灯悬停字形、减弱动态效果、悬停提示、鼠标所在屏定位

## 9. 设置持久化（本轮增量）

- [x] 9.1 `SettingsStore`：`~/Library/Application Support/FlowGrid/settings.yaml` 扁平 YAML 读写（无第三方依赖；解析容错）
- [x] 9.2 AppModel：启动读盘（含本机配对码）、2s 变更自动保存、退出前落盘；配对码刷新/编辑同步写回
- [x] 9.3 单测（往返/缺省/坏行容错）+ 实机验证（自定义 settings.yaml → 启动显示配对码 4242）

## 10. 健壮性与优化（本轮增量）

- [x] 10.1 自动重连：§10.1 五次指数退避 ±20% 抖动；认证失败/版本不兼容/对端拒绝标记不可重试（链路测试覆盖）
- [x] 10.2 并发会话守卫：同时发起/被连只保留先到者；拒绝连接本机自身监听（进程内全局出帧队列约束）
- [x] 10.3 接收目录单一入口：设置/文件屏输入/目录选择三处同步 Manager；权限状态每次开板刷新
- [x] 10.4 接收文件重名自动改名（`name (2).ext`），避免覆盖
- [x] 10.5 发送吞吐提升（512 块/拍 ≈ 64 KB/100ms）；剪贴板历史按设置条数截断
- [x] 10.6 开机自启真实注册（SMAppService；开发签名失败回退并提示）
- [x] 10.7 图片剪贴板：PNG 自动推送（受「同步图片」开关约束）、接收保留原始数据可写回；实机验证文本 + 图片双向（28 段）
- [x] 10.8 延迟告警真触发：阈值/连续次数按设置逐样本计数并 Toast（修改即时生效）；未来阶段设置项加诚实标注
- [x] 10.9 传输基础件单测：TCP 分帧、IDENTIFY 编解码、键码表、地址解析、重连退避（10 例）
- [x] 10.10 mDNS 发现：监听广告 `_flowgrid._tcp` + 浏览候选 + 按 service 连接（过期自动重连）；链路测试覆盖发现→连接
- [x] 10.11 本地网络权限提示（空态）+ Info.plist NSBonjourServices / NSLocalNetworkUsageDescription

## 验证

- [x] `cd protocol && go test -count=1 .` 全绿（零波及，含上游合并后的用例）；`go vet .` 干净
- [x] `cd protocol && CGO_ENABLED=1 go test ./cgo`（macOS 试跑，含新增会话层桥用例）
- [x] `cd mac/FlowGridCore && swift build && swift test`（9 用例）
- [x] `cd mac/FlowGridUI && swift build && swift test`（8 用例）
- [x] `cd mac && xcodegen generate && xcodebuild build` 成功
- [x] `xcodebuild test`：FFI 会话往返与冒烟全绿（7/7）——测试无宿主、构建产物放内置盘
- [x] 启动 app：进程存活、浮板逐屏窗口截取验证（connections/pairing/latency/keymap/clipboard/files/settings 七屏）；托盘浮层需人工点击菜单栏目验（系统 popover，无法程序化唤出）
- [x] `mac/README.md` 与 `AGENTS.md` §九 更新就位

## 环境备注（非代码问题）

- 沙箱化的测试 runner 打开**外置卷**上测试包时会阻塞在 open（TCC 已放行仍卡）——故测试改为无宿主，且 Makefile 默认派生数据在内置盘；macOS 27 + 外置 USB 卷组合的已知坑。
- 全屏截图受屏幕录制权限限制（只回壁纸）；`screencapture -l <windowID>` 窗口级截图可用，验证据此完成。
- 上游协议合并（45 提交）带来的适配：会话层 cgo 导出（sink+poll 形态）、`fg_keymap_load_file` 返回 `GoUintptr`、`fg_frame_validate` 改抛 §12 错误码、协议常量单一来源上移 core（删 `GoConstants.swift` 重复）。
