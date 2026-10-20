# FlowGrid — deepin (DDE 25) 客户端

基于 **DTK6 原生 Widgets**（不是 QML）的键鼠共享客户端托盘程序，对应 `design/unified/`
下的统一设计稿。协议层只经 `protocol/cgo` 消费，本目录不实现任何帧编解码。

## 形态

**托盘常驻、无主窗口**。所有界面是按需唤出、失焦即走的浮板：

| 浮板 | 宽 | 触发入口 |
|---|---|---|
| 托盘浮层 `tray.html` | 300 | 点击托盘图标 |
| 设备与连接 `connections.html` | 520 | 托盘浮层 |
| 网络延迟 `latency.html` | 520 | 托盘浮层 / 设备与连接折叠区 |
| 添加设备 `pairing.html` | 520 | 托盘浮层 / 设备与连接 |
| 键位映射 `keymap.html` | 520 | 托盘浮层 / 设备与连接折叠区 |
| 剪贴板 `clipboard.html` | 520 | 托盘浮层 / 设备与连接折叠区 |
| 文件传输 `files.html` | 520 | 托盘浮层 / 设备与连接折叠区 |
| 设置 `settings.html` | 520 | 托盘浮层 / 设备与连接页脚 |

## 构建

```bash
export PATH="$HOME/sdk/go/bin:$PATH"
export CC=/usr/bin/gcc CXX=/usr/bin/g++
export FLOWGRID_DTK_SYSROOT="$HOME/.local/dd-sysroot/usr"

cmake -S deepin -B deepin/build -DCMAKE_BUILD_TYPE=Release
cmake --build deepin/build -j4
```

### 环境相关的两个坑（别改 CMakeLists 里的这两处）

1. **`CC` / `CXX` 必须显式指向真正的 C 编译器。** 本机 `~/.local/bin/gcc` 是一个
   包装脚本，实际 exec 的是 `x86_64-linux-gnu-g++-13`。cgo 编译 `.c` 时会走 C++
   前端，`-std=gnu90` / `-Wdeclaration-after-statement` 在 `-Werror` 下直接致命失败。
   CMake 的 custom command 已经写死了 `CC=/usr/bin/gcc CXX=/usr/bin/g++`。

2. **Qt6 / DTK6 开发文件只存在于用户级 sysroot**（`~/.local/dd-sysroot/usr`），
   系统目录没有。`FLOWGRID_DTK_SYSROOT` 指定它；只加 `-I`，**不能 `-isystem` 整个
   include 目录**——那会让 sysroot 的 c++ 头遮蔽系统 glibc，libstdc++ 随即炸掉。
   头路径要加 `dtk6/{DCore,DGui,DLog,DWidget}` 四个子目录，因为 DTK 的头是无扩展名
   形式（`DApplication` / `DPalette` / `DGuiApplicationHelper`）。

运行：

```bash
export LD_LIBRARY_PATH="$HOME/.local/dd-sysroot/usr/lib/x86_64-linux-gnu"
export QT_PLUGIN_PATH="$HOME/.local/dd-sysroot/usr/lib/x86_64-linux-gnu/qt6/plugins"
./deepin/build/flowgrid-deepin
```

无头验收（依次渲染 8 屏到 PNG）：

```bash
QT_QPA_PLATFORM=offscreen ./deepin/build/flowgrid-deepin --snapshot /tmp/fgshot/s
```

### 界面自检

快照看的是「长什么样」，看不出「按下去有没有反应」。界面自检走真实控件与真实
事件路径，验后者：

```bash
./deepin/build/flowgrid-uitest     # 或：cd deepin/build && ctest --output-on-failure
```

覆盖：托盘快捷键（Ctrl+K / Ctrl+, / Ctrl+Q / Ctrl+1..3）按下去真的触发、
托盘上显示的键位与实际注册的键位是同一套、裸按键不误触发、Esc 真的收起浮层、
焦点环真的画出来了。

两个前提，缺一不可：

- **键事件必须标成「自发」**（`QEvent::setSpontaneous()`）。Qt 6 里这个函数是私有的，
  公开的转发头 `QtTest/qtestspontaneevent.h` 在 QtTest 模块里（纯头文件，不链接库）。
  没有 QtTest 开发包时自检降级为「只核对绑定、不核对派发」，并如实打印跳过原因。
- **offscreen 平台不会自动激活窗口**：得手动 `QApplication::setActiveWindow()` +
  `setFocus()`，否则没有焦点部件，Qt 根本不会去查快捷键表。

## 开发模式（dev.sh）

日常开发用这个脚本，不用先 `install.sh` 装一遍：

```bash
./deepin/dev.sh                    # 配置 + 编译（Debug）+ 直接跑起来（Ctrl+C 停）
./deepin/dev.sh --skip-build       # 跳过编译，直接跑已有产物
./deepin/dev.sh --release          # 用 Release 编译（默认 Debug）
./deepin/dev.sh --check            # 体检：界面自检 + 明暗快照 + 协议层只读校验
./deepin/dev.sh --snapshot [目录]  # 只出无头快照（默认 /tmp/fgdev）
./deepin/dev.sh --theme light|dark # 快照主题（默认明暗各出一套）
```

它和 `install.sh` 的分工：`install.sh` 装到系统位置（`/usr/local` + `/usr/share`），
需要 sudo，给最终用户用；`dev.sh` 只在开发机上跑，**不装任何东西、不碰系统路径、
不需要 root**。开发过程中每改一次就装一次，既慢又容易把「系统里那份旧的」和
「刚编的这份」搞混——`dev.sh` 用独立的 `deepin/build-dev/` 目录，两个互不干扰。

脚本自带环境变量（`CC` / `CXX` / `FLOWGRID_DTK_SYSROOT` / `LD_LIBRARY_PATH` /
`QT_PLUGIN_PATH`），上面「构建」那节手动 export 的那些不用再管。

两点值得留意：

- **快照必须显式带 `--theme`。** 不带的话应用会问宿主的 Appearance1，宿主是浅色时
  那张「深色快照」画出来就是浅色，看着像快照坏了。
- **快照落盘要数 PNG 数量。** 少落就是没成功，不能像以前那样「命令返回 0 就算过」。

比对快照时注意有三处**本来就会抖**：`s-connections*.png`（延迟读数跨过质量阈值，
状态色整个翻面，最大像素差可达 200+）、`s-latency.png`（同上）、`s-tray.png`
（托盘徽标计数）。同一二进制连跑两次，这三张也会出现同量级差异——判断回归时先
拿这个当基线，别把它们算成改动引入的。其余各屏是逐像素稳定的。

## 安装

```bash
./deepin/install.sh              # 配置 + 编译 + 系统级安装（需要 sudo）
./deepin/install.sh --skip-build # 只安装，沿用已编译好的产物
./deepin/install.sh --launch     # 装完直接拉起
```

装到 `/usr/local` **必须是系统级**，不能只装到 `~/.local`：

- **DConfig meta 固定认 `/usr/share/flowgrid/flowgrid-deepin.json`**，不搜 `~/.local/share`。
  装错位置不报错——`create()` 返回不可用对象、设置静默回退默认值，设置屏的开关看着能点、
  实际上关掉进程就没了。这是本工程最难查的一种坏法，所以脚本末尾会实打实校验这个文件。
- **自启项必须落 `/etc/xdg/autostart`**（`CMakeLists.txt` 里是绝对路径 destination，
  相对路径会被拼到 prefix 下）。装进 `share/applications` 是错的：那是菜单目录，而这个
  文件 `NoDisplay=true`，结果既不进菜单也不自启，成了一枚死文件。

装好的可执行文件带 RPATH，**直接跑不需要设 `LD_LIBRARY_PATH` / `QT_PLUGIN_PATH`**
（上面「运行」那两行是给 build 目录里的未安装产物用的）。

启动后不弹窗——托盘常驻、无主窗口，点托盘图标才出 300px 浮层。启动日志里若出现
「DConfig meta 未安装」，说明 meta 没被 dconfig 认到。

## 目录

```
dev.sh         开发模式：编译 / 跑 / 自检 / 快照。不装系统、不需要 root
install.sh     系统级安装（sudo）：/usr/local + /usr/share + /etc/xdg/autostart
src/core/    协议绑定 + 主题 + 配置（无任何 UI 依赖）
  designtokens.h  tokens.css 第 0/4 节逐条移植（几何 / 动效 / 间距 / 字号 / 圆角 / 阴影）
  theme.*         设计令牌 → DPalette 语义角色。状态色（ok/warn/danger/idle）不进 DPalette，
                 因为 DTK 没有这些语义角色，用 Highlight 推导会让同一状态四平台显示不同颜色
  hosttheme.*     DDE 专属：org.deepin.dde.Appearance1 的 GlobalTheme 判深色
  protocolapi.*   cgo RAII。GoBuffer 恰好释放一次；ClipboardSession / FileSender /
                 FileReceiver 三个会话句柄
  devicemanager.* 设备模型 + 2s 采样定时器。DeviceSource 抽象，换掉它即可接真实传输
  settings.*      DConfig 配置（非 QSettings）
src/ui/      浮板与自绘组件
  svgsubset.*     自足 SVG 子集渲染器（本机 Qt6Svg 只有运行时库没有开发包）
  icons.*         设计稿 app.js 的图标表逐条照搬
  widgets.*       StatusDot / Badge / ToggleSwitch / LatencyChip / ProgressTrack /
                 BarChart / FgButton / IconButton / DeviceCard / NavRow / ListRow
  panel.*         Panel 基类 + 浮板定位/入场动效
  hotkeys.*       托盘快捷键：绑在当前浮板上，修饰键跟着设置走
  *panel.*        8 块浮板
tests/       界面自检（真实控件 + 真实事件路径）
resources/   图标、desktop 文件、DConfig meta（flowgrid-deepin.json）
```

## 关键设计决策

- **自绘控件而非 DTK 原生控件**：设计稿把几何钉死（28px 按钮 / 36×20 开关 / 18px 徽章 /
  42px 设备图标）并要求四平台逐像素一致，而 DTK 原生控件尺寸随主题浮动。
  **尺寸服从设计稿，颜色服从 DTK 规范（DPalette）**——两条约束各归其位。
- **状态色不进 DPalette**：DTK 无 success/danger 语义角色。
- **cgo 用轮询而非回调函数指针**：Go 回调无法直接跨 C ABI；轮询也与 GUI 定时器天然对齐。
- **延迟是半 RTT**：`latencyMs = rttUs / 2000`（见 `protocol/heartbeat.go`）。
- **延迟分级 4ms / 20ms**：取自设计稿 `latency.html` 的显式告警阈值 20ms。
  不要沿用旧客户端的 2/4ms，那会把 3ms 的正常读数误判成告警。
- **托盘图标不设 `ItemIsMenu`**：设了 dde-dock 只会弹菜单，设计稿的自绘 300px 浮层
  永远唤不出来。
- **DConfig 按固定约定定位 meta 文件**：`/usr/share/<appId>/<name>.json`。本工程
  `appId=flowgrid`、`name=flowgrid-deepin`，与 `src/core/settings.cpp` 里的
  `DConfig::create()` 和 `CMakeLists.txt` 的安装规则三处必须一致。对不上的表现
  不是报错，而是 `create()` 返回不可用对象、设置全部静默回退默认值。

## 键盘与焦点

浮板是「按需唤出、失焦即走」的无主窗界面，只靠鼠标的话，键盘用户没有任何办法
把浮板收回去。因此：

- **Esc 关闭浮板**：`Panel`（520px 八屏）与 `TrayPopup`（300px 托盘层）都处理
  `Key_Escape`。「失焦即走」是鼠标语义，纯键盘用户把焦点停在浮板里时点不到别处，
  Esc 是唯一能收场的方式——DDE 控制中心、dde-dock 的浮层都是这么做的。
- **可交互控件都要能被 Tab 走到**：按钮、图标按钮、开关、下拉、分段控件、托盘入口行、
  剪贴板/传输历史行、折叠区头。入口行与历史行支持 `Space`/`Enter` 触发，分段控件
  支持 `←`/`→` 换档。
- **焦点环一律画在控件内侧**。设计稿写的是 `outline: 2px solid; outline-offset: 2px`
  （环在控件外面），但 Qt 的控件绘制区被裁到自己的矩形内，画到矩形外的部分一律被裁掉
  ——实测开关的外扩环只剩四个角。改成内收环后观感一致，且不依赖控件周围的留白。
  实色（主色）按钮上主色环等于没环，这类按钮改用 `onSolid`（白）。
- **焦点态用 `focused_` 成员而不是 `hasFocus()`**：`hasFocus()` 要求窗口处于激活态，
  无头截图里恒为假，焦点环就永远审不到。`focusIn/focusOut` 是控件自己收到的事件。
  配套的焦点样张快照是 `--snapshot` 产出的 `s-focus.png`。

## 托盘快捷键

设计稿 `ui.css .tray__kbd` 在托盘浮层底部摆着键位提示（Ctrl+1/2/3 切设备、
Ctrl+K 键位映射、Ctrl+, 设置、Ctrl+Q 退出）。这些提示先前只是静态文字，按下去
毫无反应——界面写了却按不动，等于骗人。现在是真的：

- 修饰键跟着设置屏的「切换热键」走（1=Ctrl / 2=Alt），**托盘上显示的键位提示和
  实际注册的键位是同一套**：两边都取 `core/settings.h` 的 `switchModifierName`。
- **快捷键挂在当前唤出的那块浮板下面**。Qt 的快捷键表是按窗口分的：`QShortcut`
  只有挂在某个部件上，才会出现在那个窗口所属的快捷键表里；挂在 `QObject`
  （比如 application）上则哪里都查不到，按了没反应。浮板是彼此独立的顶层窗口，
  所以 `showPanel()` 每次都把快捷键挪到新浮板下面（`TrayHotkeys::attachTo`）。
- 生效范围是「FlowGrid 的任一浮板开着的时候」。要做到**任何时候**都能按，需要全局
  热键：X11 抓键在 Wayland 下只对 XWayland 客户端有效，真正的路子是
  `org.freedesktop.portal.GlobalShortcuts`（要 DBus 会话 + 用户在系统弹窗里二次
  确认），超出 UI 范围，此处不铺。浮板全收起时提示也不在屏幕上，界面上不会出现
  按不动的键位。

## 当前边界（尚未接通的部分）

这些是**有意留白**，不是遗漏。传输层（L3：DTLS / UDP / mDNS / BLE）完全未实现，
所以：

- 设备数据来自 `LocalSampleSource`。延迟读数走**真实**的 Go `LatencyMonitor`，
  但网络往返是模拟的。真实传输落地时只需替换 `DeviceSource` 与 `fg_session_set_sink`，
  8 屏 GUI 不用动。
- 配对码是占位符（UI 里已明确标注），键位映射规则表等 YAML 加载，规则编辑未接通。
- 「取消传输」「向 X 发起配对」「恢复默认映射」需要传输层 / 规则编辑器，点击时
  会明确记录日志，而不是静默无反应。
- 无 DDE 会话或未安装 meta 时，DConfig 不可用，`Settings` 退化为进程内 Hash：
  设置屏的开关仍然可点可生效，只是不跨进程保留，设置屏页脚会显示当前状态。

## 需要在真实桌面验证的三件事

1. **深色主题**：深色路径已在真实显示（`DISPLAY=:0`，`bloom.dark`）下渲染验证过，
   面板底色取到 DTK 的 `#252525`、主色取到 deepin 活动色 `#024cca`，与 offscreen
   下的兜底取值不同——**这是对的**：真实桌面走 DPalette，offscreen 走设计稿兜底，
   两者的差异恰好说明 DPalette 路径确实生效了。仍需人眼在真实 DDE 会话里过一遍：
   面板浮在深色桌面上时，跟系统其它窗口的观感是否协调。
2. **托盘唤出**：offscreen 下 `QSystemTrayIcon::isSystemTrayAvailable()` 为 false。
   需在真实桌面点击托盘图标确认 300px 浮层能弹出。
3. **键盘链路在真实桌面上的观感**：Esc 关闭、Tab 焦点环、托盘快捷键都已在无头
   环境用真实事件路径验过（`flowgrid-uitest` + `s-focus.png`），但真实 DDE 会话下
   还要看一遍：浮板内 Tab 能走遍控件、Esc 能收起浮板、焦点环在真桌面底色上够清楚、
   Ctrl+K 这些快捷键按下去确实换板。

## 主题取值：DPalette 不可信时怎么兜底

`DPalette` 在「DTK 的平台主题插件没加载」时会**静默退化成内置浅色默认值**，而
`DGuiApplicationHelper::themeType()` 仍然照着宿主报 Dark。两边对不上，面和字就
一起串色——而且主色也跟着变（深色下 `Highlight` 会变成 Qt 默认的洋红 `#0081ff`，
不再是 deepin 的活动色）。

实测两处环境（探针打印 DPalette 各角色）：

| 角色 | 真实显示 `DISPLAY=:0`（bloom.dark） | offscreen（插件缺席） |
|---|---|---|
| `Window` | `#ff252525` | `#fff8f8f8` |
| `TextTitle` | `#d8ffffff` | `#d8000000` |
| `Highlight` | `#ff024cca`（deepin 活动色） | `#ff0081ff`（Qt 洋红） |

判据只有一个：**底色明暗跟宿主判定对不对得上**。对不上就整组中性色改用设计稿
tokens 取值（`applyNeutralFallback`），主色一并接管，避免面板在深色桌面上变成
一块刺眼的白板。这条兜底只影响 DPalette 自相矛盾的环境；真实桌面上 DPalette
自洽，走的还是 DPalette 路径。
