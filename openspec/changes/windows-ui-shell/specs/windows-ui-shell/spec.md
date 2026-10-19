# windows-ui-shell

## ADDED Requirements

### Requirement: 设计令牌唯一来源

UI 组件一律消费令牌资源（DynamicResource），不得自造颜色、字号、间距、圆角。数值与 `design/unified/tokens.css` v2 §0–§5（light/dark 全量 L2 + Windows 强调色 #0878d8/#4a9eff）一一对应；几何令牌与 deepin `designtokens.h` 同源。

#### Scenario: 令牌数值对账

- **WHEN** 审查任何新增 UI 代码
- **THEN** 其颜色/尺寸资源键可在 DesignTokens.xaml 或 ThemeLight/Dark.xaml 中找到同值定义，无字面量逃逸

### Requirement: 应用壳与导航

主窗口为 520px 面板、34px 自绘标题栏（可拖拽、可关闭）、左侧 44px 导航轨承载 7 个目的地（连接/剪贴板/文件/延迟/键位/配对/设置；托盘不是面板目的地），内容区随目的地切换。

#### Scenario: 切换目的地

- **WHEN** 用户点击导航轨条目
- **THEN** 内容区切换到对应屏，当前项高亮，窗口尺寸不变

### Requirement: 主题

浅/深两套色值严格对应 tokens.css §2/§2b；默认跟随系统（AppsUseLightTheme），设置屏可强制浅/深并持久化到 `%APPDATA%\FlowGrid\ui.json`；运行时切换不重启。

#### Scenario: 运行时切换主题

- **WHEN** 用户在设置屏切换主题
- **THEN** 全部屏立即换色，无残留旧色控件，重启后保持所选

### Requirement: 连接屏（live）

顶部状态 pill 如实反映核心链路（未连接 / 已连接 fgcore + mDNS 可用性）；设备卡列表来自 fgcore 推送（名称、平台·能力、地址），为空时显示空态；键位映射与网络延迟折叠区为设计骨架（功能待连接里程碑）。

#### Scenario: 核心未启动

- **WHEN** fgcore 不在运行
- **THEN** 状态 pill 显示未连接（idle），设备区显示空态，界面不崩溃并持续自动重连

#### Scenario: 设备上下线

- **WHEN** fgcore 广播 device.changed
- **THEN** 列表按快照整体刷新，顺序稳定

### Requirement: 托盘

托盘图标常驻，菜单含「打开 FlowGrid」与「退出」；退出同时结束 UI 进程（不杀 fgcore）。

#### Scenario: 退出

- **WHEN** 用户从托盘退出
- **THEN** UI 进程结束，托盘图标消失，fgcore 不受影响
