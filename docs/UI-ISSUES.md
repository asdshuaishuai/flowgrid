# Windows UI 与设计稿对比审查记录

> 2026-10-19 对 `windows/ui`（WPF）与 `design/unified/`（tokens.css / ui.css /
> screens/*.html / app.js）做的逐屏对比审查。方法：四路并行逐屏对照 + 令牌
> 逐值对账。`openspec/changes/windows-*` 已记录的有意适配**不计为偏差**
> （见 §4）；「待确认」条目需先与设计/规格核对再动手。
> 条目编号：U=行为缺陷，G=全局性问题，CN/CB/TR/FL/SE/KM/LA/PA=逐屏。
> 销账时保留条目并标注修复提交。

## 修复记录（2026-10-20）

> 决策背景见 openspec/changes/windows-ui-shell/design.md **D6**：托盘浮板冻结；
> 其余各屏功能划分不变、形态按 Windows 设计美学自行优化。

- **已修复**：U1（补 LatChipMinWidth=56 令牌）、U2（kind 三态过滤；DTO 无类型字段，
  以 scheme 前缀启发判链接）、U3（删死绑定，进行中行补大小/方向/对端 meta）、
  U4（记录行五态映射：完成/已拒绝/超时/校验失败/超限/失败兜底）、U6（均值 badge
  改取 LatAvgMs）；
  G1/G2/G3/G4/G5/G6/G7/G8 主体——四档延迟芯片、设备状态着色+状态中文化、离线
  idle 行、D4 置灰项补齐（同步图片/忽略密码管理器/代码·图片过滤段/边缘切换阈值/
  排除应用/接收设置四行/告警阈值区）、Toast 全接、DotPulse 落地、FontUi/FontMono
  令牌+mono 反序修正、MaxHeight=工作区-48、按钮内边距/图表横轴/MakePill 令牌化、
  顶部 1px 内高光、DeviceCard hover 浮起、Chevron 随折叠联动、ProgressBar 100% 变绿；
  CN1/CN2/CN5-CN10/CN12-CN14、CB1-CB12/CB15（预览卡三段重构、列表卡形态、分区顺序、
  分段过滤器、搜索占位与扩面、推送/写入/清空全接线）、FL1/FL5-FL11（接收设置区 D4、
  记录行方向·对端·大小·校验说明、SHA-256 页脚、拖放可用）、SE1/SE3/SE4/SE7-SE13
  （banner、分组标题卡内化、键帽热键、撤销死代码清理、事件泄漏修复）、KM1-KM3、
  LA1-LA9（列表重做、阈值区、柱形顶部圆角+500ms 过渡+固定刻度、画刷资源引用）、
  PA1-PA6（步骤三态、4 位配对码、发现设备列表接 devices.list 真数据）。
- **按 D6 授权保留（不再算偏差）**：CN5 两段式列表、FL4 每行卡片、SE2 即时生效、
  SE5 行序、LA10 事件驱动采样、CN4 面板页脚（设置入口由托盘承载）、CN11 标题栏
  热区、设置行 13px 与设备卡间距 8。
- **暂缓——等数据面/解冻**（原条目保留）：U5 托盘退出项与 TR 系（托盘冻结）；
  CN3 设备布局（fgcore 无显示器数据）；CN2 切换遮罩（随托盘切换迁移待定）；
  FL2 速度/ETA 与取消传输（无轮询节奏/无取消 API）；KM 冲突 banner（DTO 无冲突
  字段）；LA 丢包率/链路类型（LinkInfo 无字段）；配对码生成与按地址直连（协议层
  无配对码概念，PROTOCOL.md 已查证）；历史容量回读与 ClipEntryDto 类型字段
  （CoreLink 待扩展，当前链接判定用 scheme 启发）。
- **连带工程加固**：ConnectionsView 事件回 UI 线程（Dispatcher）、SettingsView
  Unloaded 补退订、FilesView 拖放类型别名（GlobalUsings +3）、构建期修复
  （XML 注释禁 `--`、Border 单子级、Spacing→Margin）。
- **追记（同日）**：主面板窗口按 openspec D7 重构为桌面形态——980×640 可调
  大小 + 左导航面板（icon+label）+ 最小化/最大化，解除 520 窄竖浮板约束；
  托盘浮板仍冻结。


## 结论速览

- **令牌层 ~95-100%**：ThemeLight/Dark.xaml 对 tokens.css §2/§2b 的全部语义色
  （含 alpha 量化、dark 主题 soft/line 保留 light RGB 三元组的细节）逐值吻合；
  DesignTokens.xaml 对 §0 几何/§2 间距/§3 圆角/§4 字号/§5 组件尺寸逐条移植。
  Windows accent `#0878D8`（light）/ `#4A9EFF`（dark）正确。
- **组件层样式已备齐但大量零消费**：Controls.xaml 覆盖 ui.css 绝大多数 class
  （SegItem / PreviewCard / ListCard / ListGroupTitle / KeyCap / LatChip 族 /
  Banner / DotPulse / Chevron / TrayItemDanger…），视图层却在用 DeviceCard
  泛用卡、裸 RadioButton、文本字符自造轮子。
- **视图装配层是主要差距**：行为缺陷 6 处（§1），整区缺失 5 处（接收设置、
  告警阈值、键位映射应用覆盖/冲突/高级区、配对设备列表/手动添加、设备布局
  折叠区无处落地）。
- **逐屏完成度估计**：connections ~75%、clipboard ~70%、settings 结构完整但
  形态系统性偏差、files ~50%、latency ~50%、keymap ~30%、pairing ~25%、
  tray 功能对齐但形态跑偏。

## §1 行为缺陷（必修）

- **[高] U1 LatChip 样式族引用不存在的令牌，一用即炸**
  `Controls.xaml:260` 引用 `{StaticResource LatChipMinWidth}`，全仓库无此键
  （DesignTokens.xaml §5 组件尺寸段没有；设计对应值 56，ui.css:279）。
  LatChip/LatGood/LatWarn/LatBad/LatIdle 五个样式一旦被任何视图套用即抛
  XamlParseException，目前因零使用而潜伏。
- **[高] U2 「文本」过滤器实际不过滤**
  `ClipboardView.xaml.cs:49,62`：`ApplyFilter` 只判断 `FilterLink`，选中
  「文本」时 `wantLink=false` 直接放行全部，等同「全部」。设计
  clipboard.html:44-48 + :187-194 按 `data-kind` 精确过滤。
- **[高] U3 进行中传输状态列绑定不存在的属性，恒空白；取消传输无实现**
  `FilesView.xaml:42-43` 绑 `{Binding State}`，`TransferInfoDto`
  （CoreLink.cs:50-66）没有 State 成员，WPF 绑定静默失败。设计
  files.html:43,65 的 icon-btn 取消交互完全无对应。
- **[高] U4 传输记录只有「完成」一档，失败也显示绿色「完成」**
  `FilesView.xaml:74-89`：图标硬编码 ✓、BadgeOk、文案硬编码「完成」。
  设计 files.html:109-163 有已拒绝/超时（badge--warn）、校验失败/超限
  （badge--danger）+ check/close/warn 三类图标。DTO 的 `Err`
  （CoreLink.cs:61-63）可用未用。
- **[高] U5 托盘浮板缺「退出 FlowGrid」**
  tray.html:111-116（danger 项 + power 图标）、windows-tray spec.md:7 均要求
  浮板含退出。`TrayPopup.xaml.cs:251 OnQuitClick` 是死代码（XAML 无引用），
  `TrayItemDanger`（Controls.xaml:657）未用。退出目前仅在原生右键菜单
  （TrayIcon.cs:28），弥补不了浮板缺失。
- **[中] U6 「均值」badge 实际取最小值**
  `ConnectionsView.xaml.cs:74-76`：`LatBadge.Text = 均值 {best}`，best 是
  在线链接延迟的 `.Min()`；`LinkInfo.LatAvgMs`（CoreLink.cs:32）可用未用。
  设计为均值语义（connections.html:109）。

## §2 全局性问题（跨屏）

- **[高] G1 状态语言未跟上数据面**：延迟四档芯片（good/warn/bad/idle，
  ui.css:278-287）全项目未用；设备卡无 active（accent 描边+浅底）/off
  （半透明 .6）着色（ui.css:265-266）；配对步骤缺 done 态（LA/PA 详列）；
  延迟页 `Where(State=="online")` 把离线设备整个过滤掉而非显示 idle「—」行
  （LatencyView.xaml.cs:44）；延迟分档阈值实现 `<2/<4` ms vs 设计 `<8/<20` ms
  （LatencyView.xaml.cs:35-36 vs app.js:290——设计示例值 2.4ms 在实现里会
  显示 warn 色）。
- **[高] G2 windows-design-completion D4「置灰渲染」被违反**
  应「照常渲染但 IsEnabled=false + 注明待后续里程碑」的项被删除或降级为文字：
  剪贴板「同步图片」「忽略密码管理器」「代码/图片」过滤钮被删
  （clipboard.html:140-147,46,48 vs ClipboardView.xaml:14-17,74-84）；
  托盘鼠标平滑/自动路由开关换成文字（tray.html:92-93 vs
  TrayPopup.xaml:130-139）；设置页两开关同理；`ClipboardView.xaml:81-84`
  静态文字「已启用」还与设计默认关（clipboard.html:149-150）相反。
- **[中] G3 图标体系空转**：Icons.xaml 几何已移植（:8-49），视图层几乎全用
  文本字符（"✓"、"▾"、"◎"、10px「发送/接收」小字——tokens.css:210 明令禁止
  8/9/10px）；chevron 不随折叠旋转（Controls.xaml:319-339 的 Chevron 动画
  样式零使用）。图标保真已挂起（windows-ui-shell design.md:35），但
  「已移植的图标不用、却用字符顶替」属装配问题。
- **[中] G4 Toast 建好未接**：`ShellWindow.xaml.cs:123 ShowToast` 已实现，
  剪贴板/设置/文件页所有操作均静默；设计写入/推送/清空/开关切换均有反馈
  （app.js:232-235,308-320）。
- **[中] G5 DotPulse 全部未接**：标题栏（ShellWindow.xaml.cs:106-113）、托盘
  头部（TrayPopup.xaml:31）、连接屏状态点（ConnectionsView.xaml:14-16）均为
  静态 Ellipse；`DotPulse`（Controls.xaml:207）与设计 dot--pulse 2s
  （ui.css:241-243）就绪未用。
- **[中] G6 字体栈未落地**：tokens.css §3 要求 Windows UI 字体
  "Segoe UI Variable Text"、mono "Cascadia Mono" 优先；DesignTokens.xaml 无
  FontFamily 令牌，全应用走 WPF 默认回退；mono 被反序写成
  "Consolas, Cascadia Mono, Global Monospace"（Controls.xaml:32）；
  FilesView.xaml:34 / LatencyView.xaml:15 各自散写字面量。
- **[中] G7 数值纪律松动（令牌外字面量/互相矛盾的取值）**：
  托盘浮板 Width=360 vs `WTray=300` 令牌未消费（TrayPopup.xaml:5 vs
  DesignTokens.xaml:13；tokens.css:17）；`ShellWindow.xaml:6 MaxHeight="620"`
  vs `--panel-max-h: calc(100vh - 48px)`（tokens.css:23，未移植）；按钮内边距
  统一 PadBadge(7) vs 设计 `0 12px` / sm `0 8px`（Controls.xaml:38,155 vs
  ui.css:194,213）；设置行字号 13 vs 12（Controls.xaml:12 TextMain=Font13 vs
  ui.css:559）；设备卡间距 8 vs 12（ConnectionsView.xaml:32,62 vs ui.css:178）；
  图表横轴 -60s vs 设计 -24s 且与自身 12 柱×2s 采样矛盾
  （ConnectionsView.xaml:209 vs connections.html:127）。
- **[低] G8 ShellWindow MakePill 字面量逃逸**：点 7px、字号 11、间距 6 硬编码
  （ShellWindow.xaml.cs:102-120），StatusDotSize/Font11 令牌均在，违反
  windows-ui-shell spec:12「无字面量逃逸」。

## §3 逐屏差异明细

### 3.1 connections（connections.html ↔ ConnectionsView，~75%）

- **[高] CN1 延迟档位芯片缺失**：设计每张设备卡右侧有四档延迟 pill
  （connections.html:36,48,60,72；ui.css:279-287）；实现延迟只以纯文本出现在
  链接行 meta（ConnectionsView.xaml:46）。样式族见 U1。
- **[高] CN2 设备状态着色与切换遮罩缺失**：device--active accent 描边+浅底、
  device--off 半透明 .6（ui.css:265-266；connections.html:29,65）——实现
  所有状态同一张白卡；链接行直接显示英文原始状态串 `online/offline`
  （ConnectionsView.xaml:45），设计与 windows-link spec.md:72 均为中文语义
  （连接中/在线/重连/离线）。切换遮罩+居中提示（app.js:127-137；
  ui.css:634-652）未实现，BrushOverlay（ThemeLight.xaml:53）零使用——切换
  已按 windows-design-completion D3 移至托盘，但遮罩反馈的取舍未记账
  （**待确认**是否有意丢弃）。
- **[高] CN3 五个折叠区只落地两个，且「设备布局」无处安放**：设计含键位映射/
  网络延迟/剪贴板同步/文件传输/设备布局五个折叠区（connections.html:79-245），
  实现只有前两个（ConnectionsView.xaml:105,191）。剪贴板/文件被「独立屏 +
  托盘导航」替代尚可由壳层适配解释，但「设备布局」（3×3 拖拽网格 + legend，
  connections.html:210-245；app.js:244-287）在 7 个导航目的地中均不存在，
  规格未记账的功能缺口。
- **[中] CN4 面板页脚 panel__foot 缺失**：「端到端加密 · IntelligentLink 优先」
  + 设置/关闭按钮（connections.html:248-254；ui.css:655-660），实现无对应，
  设置入口只剩托盘浮板一处。
- **[中] CN5 两段式列表与主按钮样式属实现自选**：设计是单一设备列表、行内
  操作为 26px 幽灵图标钮（connections.html:29-75；ui.css:216-224）；实现拆为
  Links（断开=BtnSecondary，ConnectionsView.xaml:49-51）+ Devices（连接=
  BtnPrimary 实心，:91-93）两段。行内「更多信息/重连/切换」图标钮
  （connections.html:37,73）均无对应。
- **[中] CN6 键位映射三格误包白卡**：设计 .keypair 是无框 tile、hover 才显
  surface-hi（ui.css:310-315）；实现包了三张 DeviceCard（白底+line-2 描边，
  ConnectionsView.xaml:123,142,161）；键帽缺 mono 字体与底部 1px 投影
  （ui.css:317-324 vs Controls.xaml:467-474 KeyCap）。
- **[中] CN7 延迟折叠区缺页脚**：「每 2 秒采样一次」+「详情」（
  connections.html:128-131）未实现，而同屏键位映射区对应页脚已实现
  （ConnectionsView.xaml:181-186），同屏不一致。
- **[中] CN8 纵向间距不成体系**：折叠区上边距分别 16 与 8
  （ConnectionsView.xaml:105,191）vs 设计 content 统一 gap 12（ui.css:152）。
- **[低] CN9 面板缺顶部 1px 内高光**：ui.css:81 `inset 0 1px 0
  var(--fg-edge-hi)`；BrushEdgeHi 已定义零使用（ShellWindow.xaml:17-22 只有
  外阴影，BlurRadius/Depth/Opacity 与 --shadow-panel 一致）。
- **[低] CN10 DeviceCard hover 缺 shadow-lift**：ui.css:264 vs
  Controls.xaml:251-255（仅换边框色）。
- **[低] CN11 标题栏细节**：缺 14×14 accent 应用色块（app.js:169）；关闭钮
  42×34 通栏热区 vs .wctl__flat 20×18/圆角 2（ui.css:134-135 vs
  Controls.xaml:595-596）；最小化/最大化省略属 D3 有意决策，不算偏差。
- **[低] CN12 空态微差**：标题 Medium vs 设计 w-semi、MaxWidth 300 vs 320、
  纵向间距 10/4 vs 8（EmptyState.xaml:13-19 vs ui.css:586-589）；图标用 "◎"
  字符替代 SVG（见 G3）。
- **[低] CN13 键位映射 badge「3 条生效」为硬编码演示值**
  （ConnectionsView.xaml:112），`KeymapStatusReceived`（CoreLink.cs:120）
  未接线；spec 称该区为「设计骨架」（**待确认**）。
- **[低] CN14 键位映射页脚整体右对齐**：设计 hint 居左、按钮居右两端分布
  （connections.html:99 `row between`）vs 实现打包靠右
  （ConnectionsView.xaml:181）。

### 3.2 clipboard（clipboard.html ↔ ClipboardView，~70%）

- **[高] CB1 过滤器不是分段控件**：三个裸 RadioButton 渲染成系统默认圆点
  （ClipboardView.xaml:14-17，无 Style、无隐式样式）；设计 .seg 分段控件
  （clipboard.html:43-49；ui.css:43-51），`SegItem`（Controls.xaml:399）
  已就绪未用。
- **[中] CB2 同步设置缺两行 + badge 计数不符**：缺「同步图片」「忽略密码
  管理器」（clipboard.html:140-147，D4 应置灰渲染，见 G2）；badge 从「4 项」
  （clipboard.html:131）变「2 项」（ClipboardView.xaml:66）。
- **[中] CB3 单条上限说明未用 banner 且文案改写**：设计 banner--neutral +
  info 图标（clipboard.html:152-155）；实现裸 TextCaption
  （ClipboardView.xaml:85-86），`BannerNeutral`（Controls.xaml:512）未用。
- **[中] CB4 分区顺序对调**：设计 预览→过滤搜索→历史→同步设置→页脚
  （clipboard.html:22-158）；实现 预览→同步设置→历史
  （ClipboardView.xaml:30-121）。
- **[中] CB5 搜索框缺占位文字「搜索历史」**：clipboard.html:50 vs
  ClipboardView.xaml:19-21。
- **[中] CB6 预览卡信息缩水且未用 PreviewCard**：设计 head 类型图标 +
  「来自 MacBook Pro」来源行、body mono（ui.css:358-362）、foot
  「62 B · 单帧发送 · 未分段」+ info 图标（clipboard.html:23-38）+
  .preview surface2 底/line1 边（ui.css:349-352）；实现用 DeviceCard
  （surface 底/line2 边），无来源行、无 mono、无 foot，正文 TextSecondary
  （ClipboardView.xaml:33-56）；`PreviewCard`（Controls.xaml:518）未用。
- **[中] CB7 「推送」按钮文案与样式双偏差**：设计「推送」+ btn--sm
  btn--secondary 描边 + arrowRight 图标（clipboard.html:36）；实现
  「推送到在线设备」+ BtnPrimary 实心、无图标（ClipboardView.xaml:52-53）。
- **[中] CB8 预览卡「写入本地」是空操作**：设计写 OS 剪贴板 + toast
  （clipboard.html:35；windows-clipboard-history spec.md:21）；实现仅刷新
  历史，注释自认 no-op（ClipboardView.xaml.cs:115-120）。同步关闭时收到的
  条目不入 OS 剪贴板（spec.md:30），此时按钮无实际效果且无反馈；行内
  「写入」（OnWriteClick）可用。
- **[中] CB9 历史列表形态不符**：设计单 .list 卡 + surface2 底分组标题 +
  hairline 行分隔 + hover surface-hi + 行首类型图标（clipboard.html:54-56；
  ui.css:554-570,342-345）；实现分组标题为裸 TextCaption、每行独立
  DeviceCard、无类型图标（ClipboardView.xaml:90-117）；`ListCard`/
  `ListGroupTitle`（Controls.xaml:477,483）与 Icons.xaml type/code/warn
  （:42-43,35）齐备未用。
- **[中] CB10 页脚缺「端到端加密 · 不落盘」整块说明**：shield 图标 + 文案
  （clipboard.html:162）；实现页脚只剩「清空历史」（ClipboardView.xaml:25-28），
  也无 trash 图标、28 高非 sm 的 24 高。「关闭」缺失属 D3b 有意变更，不计。
- **[低] CB11 历史行「写入」钮规格**：icon-btn 26×26 图标钮里塞文字
  （clipboard.html:67 vs ClipboardView.xaml:110-111；IconCopy 未用）。
- **[低] CB12 历史 hint 缺方向段**：「MacBook Pro → 本机 · 62 B ·
  text/plain」（clipboard.html:62）；CoreLink.cs:81 的 Hint 无方向，DTO 的
  `Dir` 字段（:77）已取到未用。
- **[低] CB13 「超限」状态缺失**（clipboard.html:113-125）：D1 入史点仅
  「广播/重组完成」，超限项不入史、状态不可达，**待确认**是否有意省略。
- **[低] CB14 分组标题「保留最近 50 条」为静态文案**（clipboard.html:55 vs
  ClipboardView.xaml:90）：设置页容量可切 50/100/200
  （SettingsView.xaml:162-167），切换后不随动（**待确认**是否接受）。
- **[低] CB15 搜索命中范围窄**：设计匹配条目全文（clipboard.html:190-192）；
  实现仅匹配 Preview 字段（ClipboardView.xaml.cs:63）。

### 3.3 tray（tray.html ↔ TrayPopup，功能对齐/形态跑偏）

- **[中] TR1 浮板宽度 360 而非 300**：tokens.css:17、ui.css:92、
  windows-tray spec.md:7 均为 300px；TrayPopup.xaml:5 字面量 360（扣阴影
  Margin 8 可视约 344），`WTray` 令牌（DesignTokens.xaml:13）未消费。见 G7。
- **[中] TR2 快速切换形态完全不同**：设计横向三等分图标钮——24×24 首字母
  色块、下方 Ctrl+N 键位提示、激活态 accent 描边 + accent-soft 软高亮
  （tray.html:38-51；ui.css:500-514）；实现纵向全宽文本钮、激活态
  BtnPrimary 实心 + 「● 」前缀、无首字母图标与键位提示
  （TrayPopup.xaml.cs:136-158）。功能等价（SwitchTo/高亮/索引一致）。
- **[中] TR3 「当前设备」卡无 accent 高亮，图标规格不符**：设计
  tray__active accent-line 边 + accent-soft 底、图标 28×28/r-ctl/accent-soft
  底 accent-text 字（tray.html:26；ui.css:490-496）；实现普通 DeviceCard
  （surface 底 line2 边），图标 42 圆形 surface3 底 text2 字
  （TrayPopup.xaml:77,84-90）。
- **[中] TR4 快捷开关组缺「键位映射」开关**：tray.html:90（开）vs
  TrayPopup.xaml:124-139（仅剪贴板同步开关）。D4 置灰清单不含键位映射，
  且 KeymapView 已实现，无省略理由。
- **[中] TR5 「键位映射 / 设置」入口组缺失**：tray.html:98-107（Ctrl+K /
  Ctrl+,）；实现设置改为头部齿轮钮（TrayPopup.xaml:37-54），键位映射入口
  无任何对应。
- **[中] TR6 行尾状态样式丢失**：lat--good 绿 pill、badge「向导」「N 项」
  （tray.html:64,68,78,82）全部变成纯灰 TextBlock
  （TrayPopup.xaml.cs:177-208）；LatChip、BadgeNeutral（Controls.xaml:259,231）
  未用。
- **[中] TR7 置灰开关被换成文字**：tray.html:92-93 是 switch 控件 + D4
  要求照常渲染；TrayPopup.xaml:130-139 换成「待后续里程碑」文字
  （Opacity 0.5）。见 G2。
- **[低] TR8 浮板头部缺 link 图标**（tray.html:20-21 vs TrayPopup.xaml:25；
  pulse 见 G5）。
- **[低] TR9 「本机」badge 文字色不符**：.badge accent-soft 底 + accent-text
  字（tray.html:32；ui.css:227-233）vs BadgeBorder accent-soft 底 + 灰字
  （TrayPopup.xaml:98-101）。
- **[低] TR10 托盘行缺图标**：devices/activity/plus/clipboard/file/keyboard/
  settings/power（tray.html:59-113）vs AddRow 纯文本
  （TrayPopup.xaml.cs:189-199），Icons.xaml 对应资源齐备未用。
- **[记录] TR11 头部多出设置齿轮与 ✕ 关闭钮**：设计头部仅标题+pill、靠失焦
  隐藏（tray.html:19-22）；实现自加（TrayPopup.xaml:37-71），有 D3b 依赖的
  合理性，**不计偏差**，留档。托盘原生右键菜单设计稿未定义，不构成偏差。

### 3.4 files（files.html ↔ FilesView，~50%）

- **[高] FL1 「接收设置」折叠区整体缺失**：自动接收/收到前先询问/接收目录+
  更改/退出时清空记录/16 MiB 参数 banner（files.html:166-199）——FilesView
  仅第 4 行注释提到，无任何对应 XAML 与 code-behind。
- **[中] FL2 进行中传输缺元信息**：文件大小 · 方向箭头 · 对端 · 速度 ·
  剩余时间（files.html:32-40）vs 仅 Peer 一行文本（FilesView.xaml:39-40）。
- **[中] FL3 底部操作区缺失**：「SHA-256 校验后交付」+「接收目录」「关闭」
  （files.html:202-208）。
- **[中] FL4 传输列表容器结构不符**：设计 .xfer 行置于 .list 卡内、hairline
  行分隔、无 hover（files.html:22-68；ui.css:382-387）；实现每行独立
  DeviceCard 带 hover 描边（FilesView.xaml:16）；记录行是无卡片包裹的裸
  ListRow（FilesView.xaml:64-94）。
- **[中] FL5 记录行第二行与右侧时间列不符**：设计「收到 · Linux Workstation ·
  2.1 MB · SHA-256 已校验」+ 相对时间 3m/12m/28m（files.html:86,91）；实现
  显示完整 Path 或「失败：…」，无方向前缀/大小/时间列（DTO 无时间字段，
  FilesView.xaml:83-84）。
- **[低] FL6 分组标题文案不一致**：设计「进行中」vs 实现「传输中」
  （files.html:23 vs FilesView.xaml:12）。
- **[低] FL7 传输图标用 10px 文字替代 SVG**：Icons.xaml
  IconUpload/IconDownload/IconCheck 未用（FilesView.xaml:25,76）；见 G3。
- **[低] FL8 百分比文字规格**：11px mono + w-semi + min-width 34px
  （ui.css:406）vs Consolas 11px 无 SemiBold 无最小宽（FilesView.xaml:33-35）。
- **[低] FL9 拖放区细节且不可拖**：缺 upload 图标（files.html:72）、分隔符
  "—" vs "–"、子行间距 4 vs 8（ui.css:410）；`AllowDrop` 未设，「拖入文件」
  实际不可用，仅点击选择（FilesView.xaml:51-60）。
- **[低] FL10 页内多画 15px SemiBold「文件传输」标题**：设计标题只在
  34px 标题栏（files.html:17 vs FilesView.xaml:6-7）；与 SettingsView（无页内
  标题）也不一致。
- **[低] FL11 标题栏「N 项进行中」pill 不实时刷新**：RefreshTitleRight 仅在
  Navigate 时调用（ShellWindow.xaml.cs:59,90-100），LastTransferCount 更新
  （App.xaml.cs:57）不触发重绘；pulse 见 G5。

### 3.5 settings（settings.html ↔ SettingsView，结构完整/形态偏差）

- **[中] SE1 顶部 info banner 与底部 warn banner 缺失**：「设置即时生效并
  同步到全部已配对设备」（settings.html:19-21）与关闭「新设备需确认」的
  风险提示（:147-149）。
- **[中] SE2 底部「恢复默认 / 取消 / 保存」操作区缺失**（settings.html:152-158）；
  实现为即时生效，属有意取舍但与设计不符，**未在 openspec 记账**。
- **[中] SE3 两设置行缺失**：「边缘切换阈值」（输入组，settings.html:71-80）、
  「排除应用」（剪贴板组，:113-119）。
- **[中] SE4 分组标题形态不符**：设计为列表卡内首行（大写、surface-2 底、
  hairline 分隔，ui.css:565-570；settings.html:24）；实现为卡外 11px 灰字
  （SettingsView.xaml:35 等）；`ListGroupTitle`（Controls.xaml:483-488）
  已就绪未用。files 页同理（FL6 同源问题）。
- **[中] SE5 组内行序与设计不符**：连接组设计 优先链路→自动路由→开机自启
  （settings.html:24-53）vs 实现 开机自启→优先链路→自动路由
  （SettingsView.xaml:38-80）；输入组设计 键位映射→鼠标平滑→边缘切换阈值→
  切换热键（:56-88）vs 实现 键位映射→切换热键→鼠标平滑
  （SettingsView.xaml:88-129）。
- **[中] SE6 文案差异集**（部分为 MVP 有意适配但非逐字一致）：剪贴板 hint
  「文本与图片，单条上限 63,240 B」（settings.html:95）vs「仅文本…」；
  「端到端加密 / XChaCha20-Poly1305」（:126-127）vs「传输加密 / DirectLink
  TCP + TLS 1.3（自签证书固定根）」（SettingsView.xaml:185-186）；优先链路/
  自动路由 hint 变「待后续里程碑」（settings.html:28,42 vs
  SettingsView.xaml:61,75）；键位映射「编辑」→「查看」且 hint 多出「与规则表」
  （settings.html:60,62 vs 96,98）。
- **[低] SE7 「已启用」徽章用错规格**：.badge--ok 高 18/内边 7 + w-semi
  （ui.css:227-233）vs PillOk 高 20/内边 8、无 SemiBold
  （SettingsView.xaml:188-189；Controls.xaml:187-195）。
- **[低] SE8 「历史条数」下拉规格不符**：.select 高 28（ui.css:541-547）vs
  ComboBox Height=24 默认模板未设计化（SettingsView.xaml:162-168）。
- **[低] SE9 「切换热键」未用 keycap 组件**：settings.html:86；
  `KeyCap`（Controls.xaml:467-474）未用（SettingsView.xaml:112 纯文本）。
- **[低] SE10 「撤销」两套代码不一致**：`OnRevokeClick`
  （SettingsView.xaml.cs:121-134，含 RefreshTrustListAsync）是 XAML 未引用的
  死代码；实际生效的内联处理器（:168-172）撤销后不主动刷新，依赖服务端回推。
- **[低] SE11 事件泄漏**：Unloaded 只退订 StatusReceived，未退订
  `TrustList`（SettingsView.xaml.cs:23 vs :18 订阅），视图销毁后处理器仍存活。
- **[低] SE12 进度条完成态缺失**：设计 100% 时 bar__fill--ok 变绿并显示
  「校验中」（ui.css:379；files.html:238-239）；ProgressBar 样式恒 accent
  （Controls.xaml:532-539）。样式未覆写 Template，圆角约 2px 非 pill
  （**待确认**，需渲染验证）。
- **[低] SE13 开关切换无 Toast**：app.js:232-235 vs
  SettingsView.xaml.cs:73-101（见 G4）。

### 3.6 keymap（keymap.html ↔ KeymapView，~30%）

- **[高] KM1 整页信息架构不符**：设计为 头部统计（badge「3 条生效」+「按应用
  自动切换」+ 新增规则按钮）→ 全局键帽映射网格（6 行含虚线 ghost「点击添加」）
  → 应用覆盖列表（2 行开关+编辑）→ 冲突 warn banner → 高级折叠区（2 开关）
  → footer（恢复默认/保存）（keymap.html:19-133）；实现只有只读的「默认跨平台
  修饰键」卡 + 「规则表/规则文件」状态卡（KeymapView.xaml:6-71）。code-behind
  仅订阅 KeymapStatusReceived 刷新两行文本（KeymapView.xaml.cs:15-32）；
  app.js:226-241,308-320 的开关 toast/折叠/按钮交互全无。
- **[中] KM2 键帽视觉未实现**：.key 34px 最小宽、mono 11 号、底部 1px 投影、
  ghost 虚线变体（ui.css:317-324；keymap.html:34-58）；实现纯 TextBlock 文本行
  （KeymapView.xaml:18-53）；KeyCap 已就绪未消费（ConnectionsView 有用）。
- **[低] KM3 页内重复大标题**：TextTitle 15px「键位映射」（KeymapView.xaml:7-8）；
  设计标题只在 titlebar（keymap.html:16），壳层已渲染 ScreenTitle
  （ShellWindow.xaml.cs:20），双标题。
- **[低] KM4 自加设计稿没有的空状态**（KeymapView.xaml:10-12）；空态规格
  微差见 CN12。

### 3.7 latency（latency.html ↔ LatencyView + LatencyChart，~50%）

- **[高] LA1 分设备列表未按设计**：设计「图标 + 名称 + 链路/丢包 hint +
  四档彩色延迟芯片」，离线设备显示 idle「—」行（latency.html:57-87；
  ui.css:278-287）；实现是未套主题样式的 GridView 四列（设备/平台/当前/
  min均max），无芯片配色、无离线行、无丢包与链路名
  （LatencyView.xaml:49-58；离线被过滤见 G1）。
- **[高] LA2 告警阈值折叠区整体缺失**：阈值 20ms 输入、连续超阈值 3 次、
  「超阈值时自动切换链路」开关（latency.html:90-117）。
- **[中] LA3 柱状图圆角/高度映射/过渡不符**：设计柱仅顶部药丸圆角、固定像素
  高度、500ms 高度过渡（ui.css:329）；实现四角 RadiusX=RadiusY=10、高度按
  最大值归一化到 4..66px、无过渡（LatencyChart.xaml.cs:22-23,36-57）。
  一致项：70px 高、12 柱、4px 间距、末柱 accent。
- **[中] LA4 标题栏右侧不符**：设计 pulse 状态点 + 实时延迟值（
  latency.html:16）；实现静态「采样中」（ShellWindow.xaml.cs:97；pulse 见 G5）。
- **[中] LA5 「链路」信息块缺失**：右侧 wifi 图标 + IntelligentLink
  （latency.html:22,25-28 vs LatencyView.xaml:11-18）；「当前延迟」标签缺
  w-semi。
- **[中] LA6 折叠区 chevron 无开合联动**：设计收起 rotate(-90deg) 200ms
  （ui.css:303-304；app.js:212-217）；Chevron 样式只 Loaded 播一次不响应
  Expander 状态（Controls.xaml:319-339），LatencyView.xaml:33-34 直接静态「▾」。
- **[中] LA7 页面 footer 缺失**：「每 2 秒采样一次」+ 导出诊断 + 关闭
  （latency.html:120-126）；壳层也未提供 footer 区。
- **[低] LA8 图表画刷缓存导致主题切换柱色残留**：TryFindResource 缓存实例
  （LatencyChart.xaml.cs:60-61），主题切换走整体替换颜色字典
  （ThemeManager.cs:55-68），DynamicResource 都会刷新、唯独柱色残留。
- **[低] LA9 大数字初始/兜底色不符**：无数据「—」初始 ok 绿
  （LatencyView.xaml:16）；事件后无在线链路落 else 变 danger 红
  （LatencyView.xaml.cs:34-36）；设计离线态应为 idle 灰（ui.css:287；
  latency.html:85）。
- **[低] LA10 采样节奏**：设计 2s 定时滚动（app.js:295-304；
  latency.html:143-151）；实现挂 LinksReceived 事件，链路离线后柱状图冻结
  （LatencyView.xaml.cs:24-30）。fgcore 推送周期是否 2s **待确认**。

### 3.8 pairing（pairing.html ↔ PairingView，~25%）

- **[高] PA1 除步骤条与配对码外整页缺失**：说明 banner「在新设备上打开
  FlowGrid → 添加设备…」、「一次性配对码」标签、「5 分钟后失效」+ 刷新按钮、
  「网络中的设备」列表（发起配对按钮、离线 badge）、「手动添加」折叠区
  （设备地址/备注名/连接）、footer（取消 / 第 2 步 共 3 步 / 完成）
  （pairing.html:28-98）；实现只有步骤条 + 6 个占位槽 + 禁用的「生成配对码」
  （PairingView.xaml:9-95）。code-behind 为空构造（PairingView.xaml.cs:6-11）；
  app.js:319,197-241 交互全无。
- **[高] PA2 步骤条状态模型不符**：设计三态 发现(done：ok-soft 底+绿勾) /
  配对(on：accent 实底) / 完成(pending)（pairing.html:21-25；ui.css:600-602）；
  实现无 done 态（步骤 1 直接 accent 实底、步骤 2/3 整体 Opacity 0.55），
  当前步错位到第 1 步（PairingView.xaml:11-44）。
- **[中] PA3 标题文案**：「添加设备」→「配对新设备」，标题栏与页内两处
  （pairing.html:16 vs ShellWindow.xaml.cs:21、PairingView.xaml:6）。
- **[中] PA4 配对码 6 位 vs 设计 4 位**（pairing.html:35-37 vs
  PairingView.xaml:52-86）。槽位 34×42、容器虚线/圆角 10/surface-2 底、槽间距
  8、内边距 12 均一致（DesignTokens.xaml:59-60 = ui.css:613）。
- **[中] PA5 步骤条布局与文案**：设计编号与文字同行（.step inline-flex
  gap 6px，ui.css:593）；实现编号在上文字在下，文案「双方就绪/输入配对码/
  建立连接」vs 设计「发现/配对/完成」（PairingView.xaml:11-43）。
- **[低] PA6 自加元素**：禁用按钮「生成配对码」+ 说明「配对码由 fgcore 在
  连接里程碑生成，当前为设计骨架」（PairingView.xaml:90-94；设计对应位是
  「刷新」按钮，pairing.html:40）。留档待规格确认。

## §4 已核对一致与有意适配（不计偏差）

**一致（可作范本的部分）**
- 颜色层：ThemeLight/Dark.xaml 全部 L2 语义色与 tokens.css §2/§2b 逐值一致
  （含 8 位 alpha 量化、hover/press 的 color-mix 推导、dark soft/line 保留
  light RGB 三元组）；accent `#0878D8`/`#4A9EFF` 正确。
- 形态层：§0 窗口几何、§2 间距、§3 圆角、§4 字号、§5 组件尺寸逐条移植；
  开关 36×20/滑块 16/行程 16/200ms/开启=ok-solid 与设计一致。
- 组件：ListRow 内边距 12,10、ListCard 圆角 10/line-2、行 hover surface-hi、
  DropZone 虚线 2px + hover accent、sm 次级按钮、Toast（底部 20/300ms/1.8s）、
  入场动画（translateY -8/scale .97/220ms）、双主题换字典机制均对齐。
- 托盘功能语义：失焦隐藏、实时计数、快速切换索引与 spec 对齐。

**有意适配（openspec 已记账）**
- 无主窗口、托盘常驻，44px 导航轨令牌成死令牌（windows-design-completion
  D3b）；标题栏仅保留关闭钮（D3）；状态 pill 语义改为 fgcore 链路状态
  （windows-ui-shell spec:34）；图标保真与动效全量挂起（windows-ui-shell
  design.md:35）；「文本 history + 必需空态」「托盘 pill 实时计数」「Ctrl+Alt+N
  热键体系」为 windows-design-completion 记录的有意变更。

**待与规格对账的取舍（建议补记 openspec，避免反复误报）**
- 设备切换遮罩丢弃（CN2）、设备布局折叠区无落地（CN3）、settings 即时生效
  vs 保存模式（SE2）、CB13 超限态不可达、CB14 容量文案静态、LA10 采样周期、
  PA6 生成配对码按钮、TR11 头部齿轮/✕。

## §5 修复优先级建议

1. **行为缺陷先行**：U1（坏引用）、U2（过滤）、U3（State 绑定）、U4（传输
   状态映射，复用 BadgeWarn/BadgeDanger）+ U5（托盘退出）、U6。
2. **「接线」一轮**：SegItem / PreviewCard / ListCard / ListGroupTitle /
   BannerNeutral / KeyCap / LatChip 族（先修 U1）/ DotPulse / ShowToast /
   Icons 接到各 View——样式已备好，成本远低于新写。
3. **补功能缺口**：FL1 接收设置、LA2 告警阈值、KM1/PA1 骨架页补全、TR4/TR5
   托盘入口。
4. **数值纪律收口**：G7 清单逐项回归令牌；G6 字体栈补 FontFamily 令牌。
5. **规格补账**：§4「待对账」清单逐项确认后写入 openspec。
