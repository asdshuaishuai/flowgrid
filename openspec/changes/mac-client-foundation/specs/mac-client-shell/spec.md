# mac-client-shell

## ADDED Requirements

### Requirement: 纯菜单栏形态（无主窗口）

macOS 客户端 MUST 以 LSUIElement 纯菜单栏形态运行：唯一常驻入口是菜单栏图标，所有界面为按需唤出的浮板；不存在常驻主窗口，不存在「窗口里套窗口」层级。

#### Scenario: 启动形态

- **WHEN** 应用启动
- **THEN** 不出现 Dock 图标，菜单栏出现模板色 FlowGrid 图标
- **WHEN** 关闭全部浮板
- **THEN** 进程仍存活于菜单栏

#### Scenario: 浮板唤出与关闭

- **WHEN** 从托盘或其它浮板打开一块浮板
- **THEN** 浮板 520px 宽、出现在菜单栏下方居中，按 220ms `cubic-bezier(.16,1,.3,1)` 入场
- **WHEN** 浮板失去焦点（key/resign）
- **THEN** 浮板关闭（orderOut）
- **WHEN** 已有一块浮板打开时打开另一块
- **THEN** 旧浮板关闭，与设计「浮板平级、单块并存」一致

### Requirement: 设计系统 1:1 复刻

`design/unified/` MUST 为 GUI 唯一来源：三层 token（L1 灰阶 / L2 语义 light+dark 双值 / L3 平台映射）、组件层、图标集、动效节奏逐值移植。

#### Scenario: 窗口几何常量

- **WHEN** 检查浮板/托盘/标题栏/内容边距/控件
- **THEN** 宽度 520/300px、标题栏 34px、内容边距 14px、按钮 28px、图标按钮 26px、开关 36×20（滑块 16px）、圆角 6/10/12/pill 与设计一致

#### Scenario: 图标集

- **WHEN** 渲染任意界面用图标
- **THEN** 使用 24 网格、1.75 描边、currentColor 的 SVG 路径集（移植自设计稿 app.js 全部图标），非 SF Symbols 近似替代

#### Scenario: 深浅色

- **WHEN** 系统外观在浅色/深色间切换
- **THEN** 全部语义色切换：surface `#ffffff/#1e2127`、text-1 `#171b21/#e9edf2`、accent `#007aff/#0a84ff` 等按 tokens.css 双值生效

### Requirement: 数值口径取自协议

界面中的协议边界值与状态分级 MUST 以 `protocol/` 实现与 PROTOCOL.md 为准，不得沿用设计稿演示旧值。

#### Scenario: 延迟分级

- **WHEN** 延迟样本 < 2ms / 2–4ms / > 4ms
- **THEN** 分别按「好/绿」「警告/橙」「差/红」呈现（协议 §9.2，`ClassifyLatency`）

#### Scenario: 边界文案

- **WHEN** 查看设置屏
- **THEN** 剪贴板单条上限显示 63,240 B（§5.3）、加密标注为 DTLS 1.3 · AES-256-GCM（§8）、延迟告警阈值默认 5 ms（`LatencyAlertThresholdMs`）、连续超阈值次数 3

### Requirement: 交互逻辑（真实数据优先）

无真实数据支撑的演示元素 MUST 移除或明确标注为样例；设备、连接、传输、延迟等状态以真实链路为准。

#### Scenario: 设备与连接

- **WHEN** 打开「设备与连接」
- **THEN** 列表仅包含本机与已连接对端（含实时延迟与断开操作），无演示设备
- **WHEN** 连接断开
- **THEN** 对端行标记为离线，接管开关自动关闭

#### Scenario: 接管/交还键鼠

- **WHEN** 点击托盘快速切换中的对端（或 ⌘1）
- **THEN** 在具备辅助功能权限时开启 CGEventTap 捕获并提示「键鼠已发往对端」
- **WHEN** 点击本机（或 ⌘2）
- **THEN** 关闭捕获，键鼠回到本机

#### Scenario: 连接设备

- **WHEN** 打开「连接设备」
- **THEN** 提供地址 + 配对码直连表单与本机配对码（复制/刷新）；自动发现标注为未启用

#### Scenario: 剪贴板过滤与搜索

- **WHEN** 在剪贴板屏选择类型分段或输入搜索词
- **THEN** 历史列表按类型/关键字实时过滤；复制按钮真实写入系统粘贴板

#### Scenario: 开关与 Toast

- **WHEN** 切换任意开关
- **THEN** 状态即时切换并按设计弹出「已开启/已关闭」Toast（1800ms 停留）

#### Scenario: 文件发送

- **WHEN** 将文件拖入发送区或点击选择
- **THEN** 经会话层发起 OFFER；对端自动接收并在 SHA-256 校验通过后落盘

#### Scenario: 切换设备遮罩

- **WHEN** 切换接管目标
- **THEN** 全屏遮罩 + 居中提示（300ms 入场、900ms 停留），当前设备卡同步更新
