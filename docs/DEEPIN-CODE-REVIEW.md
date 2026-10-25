# DDE 客户端代码审查记录

> 2026-10-22 对 `deepin/src`（DDE 25 / DTK6 客户端，16818 行）与 `deepin/tests`
> 做的全量审查，重点是脏代码与可复现缺陷。
>
> **方法**：四路并行分头扫（widgets / 应用外壳 / cgo FFI + 输入引擎 / 七屏面板），
> 外加自己做的交叉核查；每条结论都要么坐实在代码上，要么在真桌面上实测过。
> 子代理两路被环境取消，cgo FFI 与七屏面板两段由本人逐文件读完。
>
> **范围**：只读审查 + 按「只要是 UI 的问题直接都修好了」的长期指令做修复。
> 协议层（`protocol/` 下的 Go 代码）一行未动。
>
> 条目编号：H=高、M=中、L=低（脏代码）。销账时保留条目并标注修复提交。

## 结论速览

| 级别 | 条数 | 已修 | 遗留 |
|------|------|------|------|
| H    | 5    | 5    | 0    |
| M    | 6    | 6    | 0    |
| L    | 12   | 12   | 0    |
| 观察 | 2    | —    | 2    |

审查中**没有发现**：TODO/FIXME/XXX、`Q_UNUSED` 滥用、`#if 0`、裸 `qDebug()`、
硬编码主题色（逐文件 grep 无命中）、DConfig 键表与 meta 不一致（15 键逐键对齐，
且已写进界面自检）、图标名缺失、`placePopup` 高度钳位越界、SVG `Scanner` 越界读。
cgo FFI 层（`protocolapi.cpp`）的缓冲所有权、句柄注册表、错误码映射逐条核对，
未发现缺陷。

---

## H（高）

### H1 换强调色时全套派生色停滞在旧值

- **位置**：`core/hosttheme.cpp`（原 `onAppearanceChanged` 只在明暗翻转时 refresh）
- **成因**：`Appearance1` 的 `GlobalTheme` 字符串在换强调色时**不变**（还是
  `bloom.dark`），而 `onAppearanceChanged()` 当时只在 `dark_` 翻转时才
  `Theme::refresh()`。于是 accent / accentSoft / accentLine / line 系全部停在
  旧值，直到重启。树内没有任何 `paletteChanged` / `themeChanged` 连接。
- **影响**：用户换活动色，界面上的主色、按钮、开关、选中段全停在旧色。
- **修**：① 连 `DGuiApplicationHelper::applicationPaletteChanged` → 无条件
  `Theme::refresh()`（`Theme::refresh()` 每次重读一遍 DPalette，接到信号就能
  整体换上）；② `onAppearanceChanged()` 里改为无条件 refresh，不再只看明暗。
  见 `core/hosttheme.cpp:37`、`:64`。

### H2 托盘浮层的文字色在明暗切换后不可读

- **位置**：`ui/traypopup.cpp`、`ui/widgets.cpp`（`NavRow` / `ClipRow`）
- **成因**：文字色只有两种写法——写进 `QLabel` 的样式表，或写进自绘控件
  palette 的 `WindowText` 角色（实测：样式表的 `color` 会落到 `WindowText` 上，
  自绘控件因此也跟着变）。两种写法记下的都是**构造那一刻**的主题值。
  `NavRow::repaintChildren()` 只在构造与 `setDanger()` 时调用、类内无
  `Theme::changed` 连接；`TrayPopup::restyle()` 只换阴影板与 band 底色，
  头部标题、分组小标题、快捷开关行名、快捷键提示全停在旧值。
- **影响**：明暗切换后深色底上是近黑的字（读不出来），浅色底上是看不见的白字。
  托盘浮层是本应用唯一的常驻界面，一开就是整晚。
- **修**：新增 `bindTheme(scope, restyle)`（`ui/widgets.h` / `ui/widgets.cpp:52`），
  把「按当前主题重刷」挂到控件上，scope 销毁自动摘连接；`NavRow` / `ClipRow`
  各自接上；`TrayPopup` 引入 `restylers_` 清单（`ui/traypopup.cpp:297`），
  由建它的那个 `build*` 顺手登记，新增控件不必记得改 `restyle()`。
- **验**：真桌面实测——同一个托盘浮层实例（在深色主题下构造）切到浅色后整体
  换成浅色配色（深色字 / 浅底），切回深色亦然；并写成界面自检用例
  `tests/uitest.cpp`（`checkThemeFollow`），关掉修复即失败。

### H3 「失焦即走」的兜底实际不存在

- **位置**：`ui/panel.cpp`（`Panel::eventFilter` 处理 `WindowDeactivate`）
- **成因**：`Panel` 从未 `installEventFilter` 自己；唯一调用路径是
  `keymappanel.cpp` 把 ghost 格子的过滤器转给 `Panel::eventFilter`，而
  `WindowDeactivate` 是发给窗口本身的、不会发给子控件。于是那条兜底形同虚设，
  浮板能不能收起全看后端（X11 的 `Qt::Popup` 会自动关，Wayland 不会）。
- **修**：`Panel` 与 `TrayPopup` 构造时 `installEventFilter(this)`
  （`ui/panel.cpp:201`、`ui/traypopup.cpp:159`），失活事件正是发给这两个顶层
  popup 窗的；并且延后一拍再收、且只在「仍然失焦」时收——否则用户刚重新点开
  面板，那条延后的 hide 会把它又收掉。

### H4 多屏时浮板开到错的显示器

- **位置**：`ui/panel.cpp`（`placePopup` / `edgeAdjustedMargin` 用了
  `QGuiApplication::primaryScreen()`）
- **成因**：浮板位置与任务栏方位都按**主屏**算，而锚点是全局坐标。用户把托盘
  图标拖到第二块屏，点出来的浮板却开到第一块屏上；任务栏在左侧时方位也取错。
- **修**：新增 `screenForAnchor(anchor, near)`（`ui/panel.cpp:37`）——锚点落在
  哪块屏就选哪块，锚点拿不到时退回控件当前所在屏，再退回主屏；
  `edgeAdjustedMargin` 改为接受屏幕参数，调用方统一传同一块屏。

### H5 状态点的 2s 呼吸动画不重绘

- **位置**：`ui/widgets.h`（`StatusDot::setPulse` 只写成员、不调 `update()`）
- **成因**：呼吸动画是 `QPropertyAnimation` 写自定义属性推进的，属性系统的通知
  只走到访问器上，不会替我们调 `update()`。少了这一句，圆点停在初始那一帧——
  要么冻着不动，要么等别处触发重绘时「啪」地跳一下，看着像卡顿而不是呼吸。
- **修**：`setPulse` 补 `update()`（`ui/widgets.cpp:293`）；时长改走
  `tokens::kDurPulse`。

---

## M（中）

### M1 未接通的按钮点了没有任何反馈

- **位置**：`ui/settingspanel.cpp`（原 `unwiredButton` 只写日志）
- **实测**：点「管理」（授权设备管理）与「3 个」（排除应用列表）**无任何可见
  反馈**，仅 `flowgrid.settings.panel: "授权设备管理" "尚未接通"` 一行日志。
  键位映射屏那份有 toast、设置屏那份没有——同一个概念两套行为。
- **修**：统一到 `widgets.h` 的 `unwiredButton()`（`ui/widgets.cpp:1119`），
  一条 `qCInfo` + 一条 toast；`main.cpp` 的 `notYetWired()` 也补上 toast。

### M2 DConfig 拒收写入时静默丢弃

- **位置**：`core/settings.cpp`（`write()` 丢弃 `setValue` 的结果）
- **成因**：DTK6 的 `DConfig::setValue` 签名就是 `void`，没有返回值可判；
  键不在 meta 里时（meta 装在系统目录，没跟着新版本重装就是缺新键）写入被
  **静默**丢弃：界面上的开关看着生效了，进程一关就没了，用户还以为是自己没保存。
- **修**：`write()` 先用 `config_->keyList().contains(key)` 判 DConfig 认不认这个
  键（`core/settings.cpp:165`）；不认就记警告、把值留在进程内 Hash（本轮仍
  生效）、并置降级状态。设置屏挂一条横幅把「本次修改不跨重启保留」写出来
  （`Settings::degraded()` / `degradedReason()`）。

### M3 「恢复默认」与键表是两套来源

- **位置**：`core/settings.cpp`（`resetToDefaults()` 遍历 `config_->keyList()`）
- **问题**：`managedKeys()` 声明为「一处定义两处用」，实际只有界面自检在用；
  代码里没有调用点，注释与实现对不上。
- **修**：`resetToDefaults()` 取「应用管理的键」与「meta 里实际存在的键」的
  **并集**——只取 meta 会漏掉 meta 还没重装的新键，只取 `managedKeys()` 会漏掉
  meta 里有而应用不认识的键。两边都取，「恢复默认」才是真的「全部回到默认」；
  同时清掉进程内兜底的值（不清的话点完按钮开关还停在刚才拨的位置）。
  `managedKeys()` 也因此真的成了「一处定义两处用」。

### M4 设备源自行销毁后，列表与延迟读数冻住

- **位置**：`core/devicemanager.cpp`
- **问题**：源被销毁时只把 `source_` 置空，`devices_` 里那一批还留着，采样器却
  已经没得可采（`onSampleTick` 早退）。界面于是永远显示着一批已经不在的对端，
  延迟读数停在最后一帧——看着像「连不上但也不掉线」。
- **修**：源销毁时把 `devices_` / `monitors_` 清掉、停掉采样器，并通知界面。
- **注意**：收尾动作**推到事件循环里**做（`core/devicemanager.cpp:172`）。
  源是 `QApplication` 的子对象，应用退出时它跟各块浮板一起被销毁、顺序不定；
  在销毁回调里直接 `emit devicesChanged()`，浮板可能已经处在「销毁到一半」的
  状态（子控件已经没了、对象还在），它的 `refresh()` 一跑就踩到悬空指针——
  实测退出时段错误（崩在 Go 运行时的信号转发里，栈上看不出是哪一行）。
  推给事件循环后：正常运行时下一轮就收拾干净；退出时事件循环早停了，这段根本
  不会执行。

### M5 SVG 路径在第一个 `z` 处被截断

- **位置**：`ui/svgsubset.cpp`
- **成因**：「本轮没有消费任何输入」的兜底起点记在**取命令字母之后**，于是
  `Z/z` 这类不消费参数的命令对**显式**的 `z` 也成立——路径在第一个 `z` 处被
  截断，后面那些子路径（一个图标里常有三四段）全被丢掉，且一路没有报错：
  图标画得出来，只是缺了几笔。
- **修**：起点改记在取命令字母之前——显式的 `z` 至少消费了那个字母，不会触发
  兜底；只有**隐式重复**的 `z` 才真的什么都没消费，那时兜底生效，防止在字符串
  末尾空转到死。

### M6 `S`/`T` 的控制点反射规则混用

- **位置**：`ui/svgsubset.cpp`（`Q`/`T` 与 `C`/`S` 共用 `lastCubicCtrl`）
- **问题**：按 SVG 规范，`S/s` 反射的是上一个**三次**曲线（`C/c/S/s`）的第二个
  控制点，`T/t` 反射的是上一个**二次**曲线（`Q/q/T/t`）的控制点；前一个命令是
  另一族时控制点不存在，按「控制点 = 当前点」处理。原先两族共用一个变量，
  `S` 跟在 `Q` 后面会把二次曲线的控制点当成三次的来反射，`T` 跟在 `C` 后面
  也一样——画出来的曲线是歪的，而且歪得毫无规律。
- **修**：拆成 `lastCubicCtrl` / `lastQuadCtrl` 两套（含各自的有效标志）。

---

## L（低 / 脏代码）

### L1 `css()` 复制了 11 份

`ui/panel.cpp`、`ui/widgets.cpp`、`ui/pairingpanel.cpp`、`ui/filespanel.cpp`、
`ui/keymappanel.cpp`、`ui/clipboardpanel.cpp`、`ui/traypopup.cpp`、
`ui/connectionspanel.cpp`、`ui/latencypanel.cpp`、`ui/settingspanel.cpp`、
`main.cpp` 各一份，函数体相同、**注释还不一致**（部分有「必须带 alpha」说明，
部分没有）——典型漂移。
**修**：收成 `widgets.h` 的 `cssColor()`（`ui/widgets.cpp:47`），各屏改用它；
`panel.cpp` 的 `colorHex()` 同名同体，一并收掉。

### L2 `unwiredButton` 三份且行为分叉

`pairingpanel.cpp`（5 参，有 toast）、`keymappanel.cpp`（6 参，无 toast）、
`settingspanel.cpp`（3 参，无 toast、无图标）。**修**：并成
`widgets.h` 的 `unwiredButton(text, what, parent, iconName, small, accent, onClick)`，
三屏统一走它，行为一致（一条日志 + 一条 toast）。

### L3 `agoText` 三份且单位不同

`filespanel.cpp`（收 ms）、`clipboardpanel.cpp`（收 secs）、`connectionspanel.cpp`
（收 secs，输出格式又是另一套）。**修**：拆成两个语义明确的共享函数——
`compactDuration()`（列表行末尾的 `3m`，设计稿 clipboard.html / files.html 就是
这个写法）与 `agoText()`（句子里的「12 分钟前」，设计稿 connections.html 的
设备副标题就是这个写法），单位一律为秒。

### L4 `humanSize` 两份且行为不同

`filespanel.cpp`（支持 GB、KB 恒 0 位小数）与 `clipboardpanel.cpp`（不支持 GB、
KB 非整除时 1 位小数）——同一批文件在两屏里显示成两个数。
**修**：收成 `widgets.h` 的 `humanSize()`（`ui/widgets.cpp:95`）。

### L5 `Settings::e2eEncryption()` 零调用点，徽章写死「已启用」

键 `security/e2e_encryption` 在 DConfig meta 里、值 True，而设置屏的徽章是
硬编码的「已启用」——用户在 dconfig-editor 里改它毫无作用。
**修**：徽章取真值（读 DConfig），开显示「已启用」、关显示「已关闭」。

### L6 `Settings::isBackedByDConfig()` / `unavailableReason()` 零调用点

DConfig 不可用时的进程内 Hash 回退对用户完全不可见。
**修**：并入 `degraded()` / `degradedReason()`，由设置屏横幅兜住（见 M2）。

### L7 其它确认零引用的符号

`LinkProbe::available()/received()/lost()`、`hostinfo::kernelLabel()/archLabel()/
primaryMac()/screenLabel()`、`Surface::bandHeight()/footHeight()`、
`icons::exists()`、`LinkProbe::measured()` 信号。
**修**：诊断导出的内容太薄（只有设备列表，拿到报告的人不知道是在哪台机器、哪种
网络下测的），把 hostinfo 四项与 LinkProbe 三项接进「导出诊断」；真正没有
用途的（`Surface` 两个 getter、`icons::exists()`、`LinkProbe::measured()` 信号）
删掉。

### L8 `icons::icon(...).pixmap(...)` 两步走重复 11 处

`size` 要写两遍，改尺寸时漏掉一处就是同一行里两个大小不一的图标。
**修**：`icons::pixmap(name, size, color)`（`ui/icons.h:25`）。

### L9 `Collapsible` 的框样式在构造与主题回调里各写一遍

两处写法一旦漂移，主题换过之后框还留着旧色。
**修**：抽成 `Collapsible::applySectionStyle()`，两处共用。

### L10 `Collapsible::paintFocusRing` 重复文件内 helper

它做的正是文件里那个共用 `paintFocusRing` 的事（内收 2px 的 2px 环），
两份以后必然漂。**修**：改名为 `paintHeadFocusRing` 并调用共用实现。

### L11 `Collapsible` 的 ↑/↓ 分节导航是死代码

`findChildren<>(FindChildrenRecursively)` 找的是所有后代（嵌套的折叠区也在内），
而且**不含自己**——`indexOf(this)` 恒为 -1，于是「下一节」永远算成第 0 个，
与当前在哪一节无关：键盘在浮板里乱跳，看着像失灵。
**修**：只在**同级**（`FindDirectChildrenOnly`）里找。

### L12 若干零散项

- `SegmentedControl::labels_` 只写不读 → 删。
- `SegmentedControl` 的悬停档位会粘住：`enterEvent` 只在指针**进入**控件时来
  一次，从「全部」挪到「文本」时指针始终在控件内部，不再有 Enter——悬停档位
  停在上一次的那档。→ 补 `mouseMoveEvent`。
- `LatencyChip` 的质量色可能停在旧档：读数只保留一位小数，4.0ms 与 4.04ms
  显示成同一个「4.0ms」，可它们一个算良好一个算告警；原先文字没变就早退、
  不重绘。→ 质量变了也重绘。
- `Collapsible` 点击后把焦点给了 `head_`（一个默认 `NoFocus` 的普通 QWidget，
  焦点给不进去）→ 焦点给 `this`，否则焦点环画不出来、键盘也走不到这一节。
- `activeToast()` 的注释写着「还没唤出过就是 nullptr」，与实现不符（它首次调用
  就建一个）→ 订正注释，删掉 `main.cpp` 里那个恒真的空指针检查。
- `widgets.cpp` 里一条注释提到「覆写 changeEvent」，全仓并无此重写 → 删。
- `ToggleSwitch` / `FgButton` 用 `QColor(Qt::white)` 而非 `Palette::onSolid`
  → 改走语义角色。
- 图表格数 `kSlots = 12` 重复 `tokens::kChartBars`；折叠箭头 `ic = 12` 重复
  `tokens::kIconXs` → 改用令牌。
- `FgKeycap` 的横向内边距用 `kS2`(8)，而设计稿 `.key` 写的是 `padding 5×7`——
  每个键帽宽出 2px，一行键帽的边缘对不齐 → 新增 `kKeycapPadX = 7` 等令牌。
- `FgSelect` 构造时不校验 `current`，越界时 `currentText()` 返回空串，控件显示
  成一个没有文字的下拉框 → 构造时钳到有效范围。
- Toast 的 2200ms 停留时长未令牌化 → `tokens::kToastHoldMs`。
- `DeviceCard::setTrailingWidget(nullptr)` 的注释写「不改变现状」，实现却是
  「交回默认」 → 订正注释。

---

## 观察（未改，留待后续）

- **O1** 真桌面上偶发一条 Qt CSS 警告
  `QCssParser::parseHexColor: Unknown color name '#d8ffffffpx'`（一次，未稳定
  复现；浅/深两套无头快照都不出，交互序列重放也不出）。怀疑来自某个只在特定
  主题状态下构造的样式串。没定位到具体那一处，先记在这里。
  → **已定位并修复（2026-10-25）**，见下面「追加：样式表告警的定位与修复」。
- **O2** 协议层（`protocol/` 下的 Go 代码）本次一行未动；审查中在 cgo 边界上
  没有发现缺陷，因此 `docs/PROTOCOL-ISSUES.md` 没有新增条目。

---

## 修复记录（2026-10-22）

- **已修复**：H1–H5、M1–M6、L1–L12 —— 见上文各条。
- **连带新增**：
  - `bindTheme()` / `cssColor()` / `agoText()` / `compactDuration()` /
    `humanSize()` / `unwiredButton()` 六个共享助手（`ui/widgets.h`），
    收掉 11 份 `css()`、3 份 `unwiredButton()`、3 份 `agoText()`、
    2 份 `humanSize()` 的副本；
  - `icons::pixmap()` 一个助手，收掉 11 处两步走；
  - 设计令牌新增 `kDurPulse`、`kToastHoldMs`、`kKeycapMinWidth`、`kKeycapPadX`、
    `kKeycapPadY`；
  - 界面自检新增一段「主题切换后配色跟随」（`tests/uitest.cpp`），
    覆盖 H2：关掉修复即失败。
- **验证**：`deepin/dev.sh --check` 全绿（界面自检、键鼠映射表单测、浅/深各
  15 张无头快照、协议层 go vet + go test）；真桌面 1920×1080 实测明暗切换后
  同一个托盘浮层实例跟随换色（两个方向各验一次），设置屏「安全」页的
  「端到端加密」徽章取真值。

---

## 追加：键鼠方案改造中发现并修掉的缺陷（2026-10-23）

补 deepin 的捕获→对端转发通路时，撞出三个此前没暴露的缺陷。它们潜伏的原因是
**键鼠通路此前根本没接线**（`input_engine` 在 `main.cpp` 里一次都没被引用），
没人跑过，自然没人撞上。

### D1（High）载荷构造器返回空载荷 —— 7 处

`protocolapi.cpp` 的载荷构造器都写成一行：

```cpp
return GoBuffer(fg_payload_keydown(keyCode, modifiers, &outLen), outLen);
```

C++ 里**函数实参的求值顺序是未指定的**，GCC 从右往左求值，于是 `outLen`
会在 cgo 函数写它之前就被读走（恒为 0）——构造出来的 `GoBuffer` 长度是 0，
载荷成了空字节。症状是「帧发出去了，对端什么都收不到」，而且不报错。

涉及 `latencyPingPayload` / `latencyPongPayload` / `keyDownPayload` /
`keyUpPayload` / `mouseMovePayload` / `mouseButtonPayload` / `scrollPayload`
共 7 处。已全部拆成「先取指针、再构造」两行，并在段首写明**不能**合并回去。

### D2（High）鼠标按钮丢掉按下/松开状态

`input::CapturedEvent` 没有「按下/松开」这一位，而 `EventKind::MouseButton`
又不区分按下与松开——捕获线程对两种情况产出**完全相同**的事件。对端收到后
分不清「按下去」和「松开来」，按下态会一直卡住。

已给 `CapturedEvent` 补 `pressed` 位并在捕获侧写入。注入侧
`injectMouseButton(button, down)` 本来就要 `down`，只是捕获侧一直没给。

### D3（Med）对端会话析构期的空指针解引用

`PeerSession::close()` 里 `socket_->abort()` 会**同步**发出 `disconnected`，
槽函数随即把 `socket_` 置空；回到 `close()` 再执行 `socket_->deleteLater()`
就是空指针解引用 → 段错误（崩在 Go runtime 的 `runtime.sigfwd`）。

已抽出 `dropSocket()`：先把指针摘掉、断开回调，再 `abort()` + `deleteLater()`，
切断重入路径。

### 连带修的会话行为

- **拒绝路径的回应被自己丢掉**：拒绝时先 `abort()` 会丢弃发送缓冲区里尚未
  冲出的 IDENTIFY_RESPONSE，对端只会看到「连接被掐断」而收不到「拒绝」。
  改成 `flush()` + `disconnectFromHost()` 优雅断开。
- **已拒绝 ≠ 连接已断开**：断开回调原本会把 `Rejected` 覆写成 `Failed`，
  两者语义不同（前者是对端明确答复，后者是传输异常），已分开。

### 验证

- `flowgrid-inputforwardtest`：跨平台重映射（Linux Ctrl → macOS Meta）、
  KEY_UP 键码同样重映射、鼠标按下/松开可区分、滚轮 delta 不错位、
  入站帧分发到假 Injector、非输入帧不误注入、坏载荷被拒。
- `flowgrid-peersessiontest`：回环 TCP 上完成 IDENTIFY 握手（接受 / 拒绝两条路）、
  业务帧往返载荷一致、配对码不一致时帧被丢弃。
- 两者都不需要 `/dev/uinput` 权限（注入用假 `Injector`、会话用回环 socket），
  已并入 `deepin/dev.sh --check`。

---

## 追加：界面接线时发现并修掉的缺陷（2026-10-24）

把键鼠通路接到界面上时，又撞出三个缺陷。

### D4（High）设备清单永远停在启动那一刻

`DeviceManager::onSampleTick()` 只做延迟采样，**从来不重新发现设备**——
`devices_ = source_->discover()` 只在 `setSource()` 里执行一次。

后果：LAN 发现到的对端要是在客户端启动之后才出现（或者客户端启动时对方
还没开机），就**永远不会出现在设备列表里**；走掉的对端也不会消失。实测：
客户端启动后才发现的那台 Mac，整整几分钟都没出现在「设备与连接」里。

已加 `DeviceManager::refreshDevices()`：每个采样周期重新发现一次，按设备 id
保留上一轮的延迟历史（不然延迟曲线会因「重新发现」而断），集合变化时重建
采样器并通知界面。

### D5（Med）IPv4 地址被渲染成 IPv6 长串

`LanIdDiscovery` 把对端地址渲染成字符串时，16 字节的地址一律按 IPv6 展开。
但 Go 的 `net.IP` 对 IPv4 来源常给 16 字节的**映射形式**（`::ffff:a.b.c.d`），
于是界面上出现 `0000:0000:…:ffff:c0a8:1f90:28080` 这种没人看得懂的串。

已认出映射前缀并按 IPv4 点分十进制渲染。

### D6（Med）web demo 的 TCP 分帧与协议不一致

demo 自己手写了一套 **4 字节**长度前缀，而协议 §7.2、mac、deepin 都是
**2 字节** —— 两端对不上帧长，demo 与真客户端直接互通不了。
demo 的会话 HMAC 密钥也是一把写死的常量，真客户端永远算不出同一把。

已把 demo 改成 2 字节前缀（§7.2）+ 由配对码派生密钥（算法同 mac 的
`SessionKey.derive`）。改完实测 deepin 客户端与 demo 真实握手成功。

### 验证

真机（1920×1080 实桌面）跑通完整链路：发现对端 → 点「连接」→ 对端验签通过
并生成邀请 → 接受 → 双方状态变「已连接」→ 接管开关带 uinput 权限提示。

## 追加：HAL 真机测试重写时发现的问题（2026-10-25）

### D7（Med）HAL 真机测试此前「失败还打印 OK」

`input_hal_test.cpp` 的旧版在流程没走完（`app.exec()` 返回非 0）时，
只打印一行 `input_hal_test: OK` 就返回成功——失败被吞成通过。重写后改成
「流程没走完就返回失败，并把失败原因打出来」。

### D8（Med）uinput 造出来的虚拟设备读不回来（uaccess 不覆盖虚拟设备）

`/dev/uinput` 的**写**权限由 `99-flowgrid-input.rules` 的 `TAG+="uaccess"`
给到当前会话用户 ✓；但 uinput 造出来的 evdev 节点是 `root:input 0660`，
**读不回来**：虚拟设备没有物理路径，systemd 的 `path_id` builtin 取不到
`ID_PATH`，`ID_SEAT` 为空，uaccess builtin 因此跳过它（`70-uaccess.rules`
只覆盖 joystick，`73-seat-late.rules` 靠 `ID_SEAT` 找 seat）。

后果：HAL 测试的「读回验证」与「合成设备捕获」两段在普通用户下跑不了
（`sudo usermod -aG input $USER` 之后可以）。

**这不是产品链路的缺陷**：产品只需要写 `/dev/uinput`（有权限），
从不需要读回自己造的虚拟设备。已把测试改成**降级但不假装**——权限不全时
退到「只跑注入侧」（协议 → TCP → 注入照样走完，X 光标照样动），并明确
说明哪几段没验，退出码 77（skip）。

### D9（Low）HAL 测试的捕获回调跨线程写 vector

捕获引擎的回调在自己的线程上触发，旧版测试直接往主线程正在遍历的
`std::vector` 里 `push_back`——数据竞争。重写后经
`QMetaObject::invokeMethod(..., Qt::QueuedConnection)` 排队回主线程再落。

### 验证

- `flowgrid-inputhaltest` 真机跑通注入侧：注入 `dx=+120 dy=-60`，
  **X 光标实测从 `(763,486)` 移到 `(867,434)`（Δ=+104,-52）**。
  X 的指针加速会缩放位移（120 → 103/104），所以断言只判方向与「确实动了」。
- 测量前用 `EVIOCGRAB` 接管真实鼠标（**排除引擎自己的虚拟设备**，
  否则 X 收不到注入事件、光标反而不动），让位移只可能来自注入。
- `./deepin/dev.sh --check` 全绿；HAL 那条在权限不足时如实报跳过（`!`）。

---

## 追加：样式表告警的定位与修复（2026-10-25）

O1 那条 `Unknown color name '#d8ffffffpx'` 定位到了，根因是**占位符串位**。

### 根因

`deepin/src/ui/widgets.cpp` 的 `FgSelect::openMenu()` 里，下拉弹层的样式表
是这么拼的：

```cpp
"QMenu::item { padding:6px 12px; color:%4; font-size:%5px; border-radius:%4px; }"
```

`%4` 同时被用在 `color:` 和 `border-radius:` 两个位置上，而 `%4` 填的是
**文字颜色**。于是拼出来的是 `border-radius:#d8ffffffpx`——Qt 不认识这种
颜色名，抛 `parseHexColor: Unknown color name` 警告，并把**这一条声明**
丢掉。

`#d8ffffff` 正是深色主题下 `DPalette::TextTitle` 的取值——所以这条告警只在
深色主题下出现（浅色下 `text1` 是 `#171b21`，同样拼错，只是没人注意到）。
这解释了它为什么「偶发」：它取决于那一刻的主题状态。

### 影响

Qt 只丢那一条声明，`color` / `font-size` / `padding` 都还在（实测像素对比：
修前修后菜单项的字色一致）。真正的后果是**菜单项没有圆角**：选中项那一块
主色是直角的、满宽铺出去，会从菜单自己的 6px 圆角处戳出来——外框是圆的、
高亮是方的，一处毛刺。

### 修复

- 弹层样式表抽成 `fg::selectMenuStyleSheet(const Palette &p)`
  （`ui/widgets.h`），分四段各自 `.arg()`，不再用一段长格式串。
- 菜单项圆角用 `tokens::kRadiusChip`（4），并把菜单改成 `padding:4px 4px`
  ——**菜单项内缩**之后再给圆角，选中项才是一枚完整的圆角块，不会戳出菜单
  的圆角。改前 / 改后 / 三种候选的渲染对比见提交说明。
- `QMenu` 本体的圆角保持 `tokens::kRadiusCtl`（6），与它所属的下拉框同一档。

### 回归守卫（这次真正补上的东西）

`deepin/tests/uitest.cpp` 新增「样式表」一段，两条检查：

1. **八屏里没有漏替换的占位符**：遍历托盘浮层与七个面板的全部子控件，
   找样式表里残留的 `%<数字>`。
2. **所有样式表都能被 Qt 无告警地解析**：装上 Qt 消息处理器，把每一张
   样式表丢进一个一次性的空控件强制解析（`ensurePolished` + `polish`），
   收告警。有告警就说明这张表是坏的。

变异测试确认过：把颜色塞回 `border-radius:` 的数值槽，自检立刻报
「样式表解析告警：QCssParser::parseHexColor: Unknown color name
'#e9edf2px'」并以非零码退出。

### 顺带修掉的：HAL 测试的「假失败」

同一次跑测里 `flowgrid-inputhaltest` 报了一条 `FAIL: X 光标往右移动了`。
查下来**不是产品缺陷，是测试自己脆**：

- 测量前光标停在 x=1919（1920 宽屏幕的最右一列），注入的 +120 被屏幕
  边界夹住，光标不动 → 「没往右走」其实是撞墙。
- 修法：测量前用 `XWarpPointer` 把光标挪到屏幕正中。
  **踩到一个坑**：`dest_w` 传 `None` 时这台机器上的 X 把 `dst_x/dst_y`
  当成**相对当前指针的偏移**——在 (960,540) 处调
  `XWarpPointer(..., None, 0,0,0,0, 960,540)` 会跑到 (1919,1079)（右下角）。
  传 `root` 才是绝对坐标，实测落在 (960,540)。
- 挪不动时**不判方向**（贴着屏幕边时方向断言失真），如实打印「跳过方向核对」，
  不拿一个必然失败的断言把真问题盖住。

修完实测：`(960,540) → (1063,488)  Δ=(+103,-52)`，与之前在屏幕中部的
那次数值一致。
