## Context

design/unified 的 clipboard.html（预览卡+过滤搜索+50 条历史+同步设置+清空）、settings.html（连接/输入/剪贴板/安全四组）、tray.html（300px 浮板：状态头+本机卡+快速切换+入口+快捷开关+退出）。单机验收：双进程 + devices.add 注入。

## Goals / Non-Goals

- Goals: 三屏按设计稿形态落地；历史/开关/设备管理真实可用；单机可全链路验收
- Non-Goals: 图片剪贴板、IntelligentLink、鼠标平滑、边缘切换、密码管理器排除、YAML 热重载、多显示器

## Decisions

### D1. 剪贴板历史 = fgcore 内存环形存储
hub 持 `[]ClipEntry{Text, Source, Dir, Size, Time}`，容量 IPC 可调（默认 50，§设计稿「保留最近 50 条」）。入史点：本地广播时（source=本机）与对端重组完成时（source=对端名）。仅文本（P3 图片无同步源）。UI 的「写入本地」经 `clipboard.entry` 取全文后由 fgcore 写 OS 剪贴板（回声抑制照旧）。

### D2. 开关三件套 = fgcore 运行时可变量
`clipboard.enable`（跨端同步总开关，关 = 本地变更不广播、入站不写回）、`config.autoaccept`（新设备需确认 = !autoaccept，引擎 atomic 可变）、`clipboard.cap`（历史容量）。开机自启 = HKCU `Run` 键（UI 直接读写，指向当前 exe 路径）。

### D3. 托盘浮板 = 300px 无边框 WPF 窗，复用令牌
托盘左键切换显示（定位屏幕右下角、工作区上方），失焦（Deactivated）隐藏；内容按 tray.html 分组；快速切换按钮 = `SwitchTo(index)`，索引与本机/在线序一致；入口项打开主窗口并导航到对应屏；退出复用 App.IsExiting 流程。

### D3b. 无主窗口，托盘常驻（用户明确的形态要求）
应用启动**不创建任何主窗口**：`ShutdownMode=OnExplicitShutdown`，生命周期锚在托盘图标上——左键唤出浮板、右键菜单、双击打开主面板；520 面板（ShellWindow）按需从浮板/菜单创建并常驻隐藏（关闭=收起，退出仅经托盘）。面板标题栏文案随屏切换（对齐各屏 `data-titlebar`）。

### D4. 置灰即诚实
设计稿中依赖未实现能力的项（IntelligentLink、自动路由、鼠标平滑、边缘阈值、图片/代码过滤、同步图片、排除应用）照常渲染但 `IsEnabled=false` 并在 hint 注明「待后续里程碑」——形态对齐设计稿，能力边界如实。

## Risks / Trade-offs

- 历史仅内存不落盘（设计稿「不落盘」页脚与本实现一致）
- 托盘浮板定位用鼠标/任务栏象限近似，多显示器细节后续打磨
- HKCU Run 自启在 UAC 提权需求下未来可能要改任务计划（fgcored 里程碑）
