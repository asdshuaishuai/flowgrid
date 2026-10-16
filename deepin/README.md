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

## 需要在真实桌面验证的两件事

1. **深色主题**：offscreen 下 `Appearance1` 不可达，代码回退到
   `DGuiApplicationHelper::themeType()`（light）。深色路径在位但从未在真实 DDE
   会话跑过。
2. **托盘唤出**：offscreen 下 `QSystemTrayIcon::isSystemTrayAvailable()` 为 false。
   需在真实桌面点击托盘图标确认 300px 浮层能弹出。
