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
  *panel.*        8 块浮板
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

1. **深色主题**：offscreen 下 `Appearance1` 不可达，代码回退到
   `DGuiApplicationHelper::themeType()`（light）。深色路径在位但从未在真实 DDE
   会话跑过。
2. **托盘唤出**：offscreen 下 `QSystemTrayIcon::isSystemTrayAvailable()` 为 false。
   需在真实桌面点击托盘图标确认 300px 浮层能弹出。
3. **键盘链路**：offscreen 下浮板拿不到真实焦点，Esc 关闭与 Tab 焦点环只做过
   逻辑与样张层面的验证（`s-focus.png`）。需在真实桌面确认：浮板内 Tab 能走遍
   控件、Esc 能收起浮板、Tab 走到某个控件时焦点环清晰可见。
