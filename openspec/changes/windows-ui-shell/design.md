## Context

design/unified tokens.css v2 三层令牌（L1 Primitive 灰阶 / L2 Semantic light+dark / L3 Platform 强调色）；deepin designtokens.h 是同源移植先例（几何/排版/圆角/间距逐条对应 §0–§5，颜色走主题解析）。WPF 侧动态资源体系与 CSS 变量天然同构：颜色用 DynamicResource + 运行时换字典实现主题切换。当前骨架 UI 为功能原型样式，MainWindow 单页。

## Goals / Non-Goals

- Goals: 令牌 1:1（组件不得自造颜色/尺寸）；壳/导航/主题可用；连接屏 live；其余屏空态齐整；协议层零改动
- Non-Goals: 对端连接/注入/传输接线、托盘浮板（tray.html）、动效全量（入场/脉冲）、像素级图标重绘

## Decisions

### D1. 令牌三层映射：Tokens（形）+ ThemeLight/ThemeDark（色）
几何/排版/间距/圆角（§0、§2–§5，含 designtokens.h 的 Qt 适配注释）进 `DesignTokens.xaml`，主题无关；颜色（L2 语义 + L3 Windows 强调色 #0878d8/#4a9eff）进 `ThemeLight.xaml`/`ThemeDark.xaml`，运行时整体换字典，组件一律 DynamicResource。 pill 圆角 = 高度一半，用样式内绑定高实现（对应 designtokens.h pillRadius()）。

### D2. 壳层导航 = 520 面板 + 左 44px 导航轨
设计稿各屏是独立 520 面板（HTML 页），未定义应用壳。桌面应用需要常驻目的地切换：左轨 8 项（连接/剪贴板/文件/延迟/键位/配对/设置），内容区 476px（--pad-content 内边距不变）。托盘菜单直达目的地。此为壳层适配决策，不改任何设计令牌。

### D3. 自绘标题栏 34px
WindowChrome 摘掉系统栏（设计稿 --h-titlebar=34，三平台一致），自绘标题 + 关闭钮；拖拽用 WindowChrome ResizeBorder + DragMove。

### D4. 主题跟随系统，设置可覆盖
默认读注册表 AppsUseLightTheme；设置屏三档（跟随系统/浅/深）存 `%APPDATA%\FlowGrid\ui.json`。运行时切换 = 换合并字典，不重启。深浅两套色值严格取 tokens.css §2/§2b。

### D5. 空态复用同一组件
ui.css `.empty`（圆形底 40 + 标题 + 说明）做成 EmptyState 控件，6 个骨架屏共用；折叠区（section/section__head/body）做成可复用样式 + Expander。

### D6. 形态自由度授权与托盘冻结（2026-10-20 用户决策）
- **托盘浮板（TrayPopup/TrayIcon）形态冻结**：UI-ISSUES 台账 TR 系差异不再追踪（仅留档案）；浮板「退出 FlowGrid」「键位映射开关/入口」等功能缺口待解冻后处理。
- **其余各屏**：功能划分固定不变，布局/排列/交互按 Windows 设计美学自行优化——设计稿（screens/*.html）从逐像素约束降级为结构与文案参考。
- 授权内保留的形态（不再视为偏差）：连接屏「链接 + 设备」两段式列表、文件页每行独立卡片、设置页即时生效（无保存/恢复钮）、设置组内行序、延迟页事件驱动采样。
- **空壳不建**：无数据面的分区不做骨架——设备布局折叠区（fgcore 无显示器/槽位数据）、键位冲突 banner（KeymapStatusDto 无冲突字段）、传输速度/ETA（无轮询节奏）等，待数据面落地再接。

## Risks / Trade-offs

- 导航轨占 44px 宽是设计稿之外的壳层添加，连接屏内容区比纯浮板窄——内容栅格按比例收缩，令牌不动
- NotifyIcon 引 UseWindowsForms 互操作：仅托盘图标与菜单，不引入 WinForms 视觉
- 空态骨架屏的用户预期管理：页内注明「待对端连接里程碑启用」，避免「点了没反应」

## Open Questions

- 图标体系：设计稿 SVG 子集（deepin 已描边/淡底/实体三层重绘）vs Windows 图标字体——后续像素保真变更定
- 托盘浮板 300px 的 WPF 无边框窗与定位策略——托盘里程碑定
