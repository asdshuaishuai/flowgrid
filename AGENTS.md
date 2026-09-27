# FlowGrid 项目 Agent 开发哲学

> 这是多年代码工作总结出来的经验。每一条都值得反复思考。  
> 程序员趁早想通为好，后面的路才能走得越顺。

---

## 一、核心原则：代码是负债

### 1. 代码是负债，不是资产
每行新代码都意味着维护成本。删掉一行比写一行更值得骄傲。**少即是多。**

### 2. 简单即美
控制复杂度是编程的总则。好方案 = 满足需求的最简方案。能用一个 `if` 解决的问题别上模式，能用一个函数解决的不拆三个模块。

### 3. 不过度设计
只解决当下确实存在的问题。扩展性留在接口层就够了。不要为未来五年可能发生的假设写一千行抽象。`YAGNI` — You Aren't Gonna Need It.

### 4. 设计两次
第一方案几乎不会最优。多做备选对比，**设计多两小时省实现两周。** 动手前先写三种不同方案，问自己：如果这行代码要维护十年，当前方案是最不痛苦的吗？

---

## 二、需求与问题

### 5. 追问真需求
用户给的永远是方案而非问题。追问 "为什么"，找到本质需求，往往有更优解。用户的第一个提议通常不是最优解，而是最容易想到的解。

### 6. 观察用户
别猜用户想要什么，直接看操作轨迹。一条反馈背后是上千沉默用户。用户说"慢"的时候，可能不是网络慢，而是等待过程中没有反馈。

---

## 三、架构与工程

### 7. 数据为王
系统核心是数据。代码可以将就，**数据结构必须清晰合理。** 先设计数据流，再写代码。数据不对，逻辑再好也是空中楼阁。

### 8. 接口服务使用者
好用比好实现重要。复杂性封装内部，对外暴露最简接口。如果调用者需要读三个文档才能用，接口就设计失败了。

### 9. DRY（不要重复）
一切重复都是膨胀的种子。消除重复代码，将重复流程自动化。但注意：DRY 不等于「把两个地方看起来像的东西抽象成一个函数」，而是「把语义重复的东西抽象成单一源头」。

### 10. 测试改善设计
不好测 = 结构不好。单测不只防回归，更倒逼更好的架构。当你发现某个模块很难写测试，不是测试问题，是模块耦合问题。

### 11. 为故障设计
别幻想消灭故障，**为故障准备对策**：限流、熔断、降级。优雅降级比完美运行更可贵。系统不会因为你写了完美代码就不出故障，但会因为你没写 fallback 就全崩。

### 12. 接受腐化
没有完美架构。让腐化慢些，做好隔离，留有余地就够了。与其追求永远不腐化，不如追求腐化时可以低成本重构。

---

## 四、调试与质量

### 13. 追查根因
不治表面症状，多问几层为什么。追查过程才是成长最快的时候。看到报错就改报错行是修表不治根，找到为什么这行会出错才是解决之道。

### 14. 先怀疑自己
出 bug 先查自己代码。**广泛使用的框架几乎不会错。** 你的代码有 1000 行，Qt 有 100 万行，OpenSSL 有 50 万行。概率上，bug 在你这里。

### 15. 命名即文档
好命名胜过注释。精确一致词达意，**长度与作用域成正比**。局部变量可以 `i`，全局变量必须 `device_connection_timeout_ms`。函数名是动词，变量名是名词，布尔是 `is_`/`has_`/`should_`。

### 16. 注释写为什么，不写是什么
不写显而易见的，只揭示代码无法表达的意图。代码说「做了什么」，注释说「为什么这么做」。如果注释在解释代码怎么工作，说明代码本身没写清楚。

---

## 五、交付与迭代

### 17. 早部署常交付
别堆到最后梭哈，**越早暴露问题修复成本越低。** 小步快跑，频繁集成。一个没上线的功能对用户价值为零，一个上线但有 bug 的功能可以逐步修复。

### 18. 勇于重构
系统病了就治。测试保驾护航，重构投入会在生命周期中数倍回报。害怕重构的系统只会越来越烂，直到重写。定期重构不是「有时间的额外工作」，而是「维持开发速度的必要工作」。

### 19. ROI 思维
先 MVP 后迭代，投入与收益匹配。不要因为"这个功能很酷"就做，要问"这个功能值多少钱的开发成本"。如果收益不明确，先做一个能验证假说的最小版本。

### 20. 持续学习
读源码、找导师、多分享。学能改变行为的东西，别在舒适区重复。如果一个技术你学完后，下个项目不会用，那就没真正学会。真正学会的是那些让你不由自主就用的东西。

---

## 六、协作与沟通

### 21. 软素质决定评价
做问题终结者，事事有回应，及时同步风险，把事做完。一个人解决问题的闭环能力，比写了多少行代码更重要。别人问你一个 bug，修完回复 "已修复，原因是 X，已验证 Y" 是终结者；修完不回复是挖坑。

### 22. 学会提问
带上下文、带日志、用对方能懂的词。别问浪费彼此时间的问题。好问题 = 做了什么 + 期望什么 + 实际得到什么 + 已经排查过什么。提问前自己先花 15 分钟排查，不是「我想省事」而是「我想学东西」。

### 23. 保持节奏
编程是马拉松不是冲刺，留出时间学习和休息。连续 14 小时编码的效率，不如 8 小时高质量 + 1 小时散步。疲惫时写的代码，第二天要花双倍时间重写。

---

## 七、AI 与工具

### 24. 善用 AI，但别依赖 AI
AI 能写代码不代表你可以不懂代码。把 AI 当加速器，审查输出比写 prompt 更重要。AI 写的代码可能「能运行」但「不能维护」。你不需要成为写得比 AI 快的人，但需要成为能判断 AI 写得好不好的人。

### 25. 像负责一辈子那样写代码
如果每行代码都会公开到朋友圈，你一定写得更好。写代码时想象三个月后的自己来看这段代码——如果你会骂自己，现在就重写。

---

## 八、本项目特定原则

### 26. Protocol 是权威
`design/PROTOCOL.md` 是 wire format 的唯一真相来源。任何帧结构、发现包、握手序列、键码表的改动，必须同步更新 PROTOCOL.md，否则协议不同步导致跨平台兼容性问题。

### 27. 安全优先
- 所有 `unsafe` 代码必须附带 `SAFETY` 注释
- 涉及 FFI 的指针操作（Go cgo 返回的 `*mut u8`）必须立即释放
- DTLS 证书和密钥管理是安全边界，不能为了简化而走捷径
- uinput 设备权限错误必须有清晰的用户提示（`99-flowgrid.rules`）

### 28. 跨平台兼容
Go 协议库是共享的。修改协议库时，必须确认 macOS / Windows 客户端也能编译通过。协议版本号是跨平台契约，`1.0` 不能随意升级。

---

## 九、仓库实况速览

> 日常改代码前先读这节。文档语言：中文为主 + 英文术语。

### 项目现状
FlowGrid 是一套键鼠共享软件（类 Synergy/Logitech Flow）。Linux 有两个客户端：**KDE 客户端**（`kde/`，Qt Quick/QML + Kirigami）和 **deepin 客户端**（`deepin/`，**DTK 原生 C++ Widgets**，UI 参考 `design/flowgrid-mac-*.html` 设计稿，deepin 蓝主色）。两者共享同一 Rust core 与 Go 协议库，但 UI 桥不同：KDE 走 cxx-qt，deepin 走纯 C ABI。macOS / Windows 仅有设计规格，没有代码。`CLAUDE.md` 还停留在"纯设计阶段"的描述，已过时，以本文件和实际代码为准。

### 目录地图
```
design/PROTOCOL.md        — 协议规范 v1.0（唯一权威，见原则 26）
design/*.html             — 各平台 UI 高保真原型
protocol/                 — Go 协议库（github.com/flowgrid/protocol），平台无关逻辑，一个文件对应规范一节
protocol/cgo/bridge.go    — cgo 桥，构建产物为 c-archive（libflowgrid_protocol.a/.h）
kde/rust/                 — KDE 客户端 Rust workspace（edition 2024）：
  core/                     hal/（uinput 注入）、keymapper/、transport/（near_link/direct_link）、
                            protocol_ffi.rs（FFI 调 Go c-archive）、cert_manager、device_manager
  cxx-qt-bridge/            cxx-qt 桥 + qml/（KDE QML）
  app/                      main.rs 入口
kde/CMakeLists.txt        — KDE 构建入口：先编 Go c-archive，再 cargo build --release
deepin/                   — deepin 客户端（DTK 原生，无 QML/cxx-qt/Kirigami）：
  native/                   Rust staticlib：fg_core_* 纯 C ABI（JSON 数据 + 轮询事件），复用 kde/rust/core
  ui/                       C++ DTK Widgets（main_window/add_device_dialog/latency_panel/core_client）
  CMakeLists.txt            Qt6 Widgets + Dtk6 + 两个静态库链接；udev 规则与 service 复用 kde/resources/
openspec/                 — 规格驱动开发：changes/archive/<日期-名称>/{proposal,design,tasks}.md + specs/
```

### 实际功能链路（两个客户端共用）
- **Target 注入**：入站 HID 帧 → device_manager `inject_hid` → KeyMapper 重映射 → uinput。KEY_UP 是逐键语义：修饰键通过 0xE0-0xE7 键码帧独立按下/释放，不再随 KEY_UP 批量释放。
- **Host 捕获**：`core/src/capture/mod.rs`（InputCapture）读 /dev/input/event*（EVIOCGRAB 独占），linux→HID 翻译用 `hal/keymap_tables.rs`（完整双向表，单一来源），REL 事件 5ms 合并后发 MOUSE_MOVE；deepin UI 的"控制其他设备"开关经 `fg_core_set_capturing` 启停。使用需把用户加入 input 组并安装 udev 规则（Target 注入同理）。

### 构建与测试
```bash
cd protocol && go test ./...                    # Go 协议库测试
cargo test --manifest-path kde/rust/Cargo.toml  # Rust 单测（core 各模块都有 #[cfg(test)]）
cmake --build kde/build                         # KDE 完整构建
cmake -B deepin/build -S deepin && cmake --build deepin/build   # deepin 完整构建
```
deepin 构建要求：Qt6 + DTK6 dev（libdtk6widget/gui/core-dev）+ qmake6 + pkgconf。**ZCode 运行在玲珑容器内，宿主桌面（Qt6/DTK/dde-dock）在容器外**：容器内构建用用户级自举环境 `. ~/.local/bin/fgenv.sh`（含 cmake 3.31.6 官方二进制、make、g++-12、pkgconf、Qt6/DTK 运行库与开发头，全部在 ~/.local/qt6-dev），configure 需 `-DCMAKE_PREFIX_PATH=~/.local/qt6-dev/usr/lib/x86_64-linux-gnu/cmake`（CMake 会在 configure 时固化 cargo 所需的 OPENSSL_DIR/PKG_CONFIG_* 等值）。CMakeLists 已把 OpenGL/OpenSSL/Cups/Vulkan 的 INCLUDE_DIR 隔离到 build/safe-include，防止用户树的 glibc 头以 -isystem 混入破坏 libstdc++（concurrence.h 报错即此症）。验证窗口：容器内无 xdotool，用 `gcc-12 /tmp/xlist.c -lX11 -lxcb` 自制枚举器查宿主 X 树。core 的 `build.rs` 用 `CARGO_MANIFEST_DIR` 向上三级定位 `protocol/cgo`，从任何客户端构建都能找到。单独 `cargo build` 之前必须先有 Go c-archive。

### 架构边界（改动的固定顺序）
wire format / 键码 / 握手有任何变动：**PROTOCOL.md → protocol/ Go 库 → protocol/cgo → Rust core（protocol_ffi.rs）→ UI 层**。Go 库里不写 Linux 专属逻辑；Linux 专属代码只进 `kde/rust/core/src/hal/`（两个客户端共用）。UI 层各自独立：KDE 是 cxx-qt 桥 + QML；deepin 是 `deepin/native` 的 C ABI（fg_core_*，JSON 数据、事件轮询，见 native/flowgrid_core_capi.h）+ DTK C++。改 core 的 DeviceManagerClient/KeyMapper 公开 API 时，两个桥（cxx-qt-bridge 与 deepin/native）都要同步。

### 工作流与 gotchas
- 功能变更走 OpenSpec 流程（propose → apply → archive → sync，命令模板在 `.opencode/commands/opsx-*.md`）
- uinput 权限依赖 `kde/resources/99-flowgrid.rules`（udev），随 CMake 安装；还有 systemd user service 和 .desktop 文件
- **deepin UI 视觉必须完全符合 DDE 风格**：`design/flowgrid-mac-*.html` 只作布局与交互参考，配色/控件/字体一律用 DTK 原生（DHeaderLine、DBackgroundGroup、DSuggestButton、DWarningButton、DSwitchButton、DSpinner、DPalette 语义角色），禁止硬编码主题色/背景色 stylesheet（语义状态色如延迟红绿黄除外），保证深浅色主题自动跟随
- deepin 应用身份与单实例：applicationName 必须等于可执行名（flowgrid-deepin），desktop 文件 ID 同名（resources/flowgrid-deepin.desktop），否则 dde-application-manager 把进程归到启动者（如 zcode）；首实例注册 org.flowgrid.DDE（/org/flowgrid/DDE，Q_CLASSINFO 固定接口名，ExportAllContents 导出 CoreClient），二次启动经 D-Bus 调 requestShowMainWindow 唤醒首实例后退出
- deepin 字体与托盘图标：大号展示字用 `DFontManager::get(T1, base)`（DTK6 类名是 DFontManager，无 instance()，公开构造自建实例，T1-T11 随系统缩放），不要硬编码 pixelSize；托盘图标按 tray skill 提供亮暗两套（resources/flowgrid-deepin{,-dark}.svg），经 hostThemeChanged 在 refreshTray 里切换
- deepin UI 细节规范（dtk-development gotchas 2.1/2.2）：禁止 QPalette::PlaceholderText，一律 `Theme::applicationPalette().color(DPalette::TextTips/TextTitle/ItemBackground)`（theme.h 提供唯一入口，底层是 DGuiApplicationHelper::applicationPalette）；托盘弹板用 `DBlurEffectWidget`（BehindWindowBlend + AutoColor + 圆角 12）而非自绘半透明；操作按钮用 `DIconButton(QStyle::SP_*)`，进度条用 `DProgressBar`；不要给 DTK 控件挂任何 QSS（会冻结 chameleon 绘制）
- deepin 主题跟随：DGuiApplicationHelper::themeType() 在 Deepin 25 上探测不到深色（新属性是 Appearance1/GlobalTheme="bloom.dark"）；CoreClient::applyHostTheme() 直接读该属性并订阅 PropertiesChanged 实时切换 palette（QDBusVariant 解包，不能直接接 QString）
- deepin 托盘走 SNI 协议（dde-shell 的 dde-dock 提供 org.kde.StatusNotifierWatcher）：QSystemTrayIcon 设置 Title/Id/ItemIsMenu 属性保常驻，托盘菜单含 Host 捕获开关；开机自启装 `/etc/xdg/autostart/flowgrid-deepin-autostart.desktop`（cmake --install，绝对路径 destination）
- 运行未安装的二进制时用 `FLOWGRID_QML_PATH` 指向 QML 目录，否则找不到界面
- `kde/tests/` 目前是空目录，测试在 Rust `#[cfg(test)]` 和 Go `_test.go` 里

---

*每条原则都对应着一个曾经踩过的坑。写下来，是为了以后不再踩。*  
*最后：对自己诚实，对代码诚实，对团队诚实。*
