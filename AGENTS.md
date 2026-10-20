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
- 涉及 FFI 的指针操作（cgo 返回的缓冲）由调用方经 `fg_free_buffer` 释放，且只释放一次
- DTLS 证书和密钥管理是安全边界，不能为了简化而走捷径

### 28. 跨平台兼容
Go 协议库是 wire format 的唯一权威实现。未来任何平台的客户端都必须经 `protocol/cgo`（或直接引用 Go 库）消费协议，禁止自行实现帧编解码。协议版本号语义见 PROTOCOL.md §2.3：版本 1 即当前线格式（v1.0 从未对外发布），不兼容变更必须升号或加能力位。

---

## 九、仓库实况速览

> 日常改代码前先读这节。文档语言：中文为主 + 英文术语。

### 项目现状
FlowGrid 是一套键鼠共享软件（类 Synergy/Logitech Flow）。仓库现含：

- **Go 协议库**（`protocol/`）：wire format 编解码 + 会话层（剪贴板/文件传输）+ cgo 桥，仍是唯一权威实现；
- **macOS 客户端**（`mac/`）：`design/unified/` 视觉体系 + Go 协议库 FFI 接线；**DirectLink TCP 传输与 HAL（CGEventTap 捕获/注入）已接通**（双实例实测：握手/剪贴板/文件/延迟）；DTLS/ECDH、mDNS/BLE 发现与多设备路由留接缝（见 `mac/README.md`）；
- **Windows 客户端**（`windows/`）：Go 会话核心 fgcore + WPF UI，骨架阶段；
- **deepin 客户端**（`deepin/`，DTK6 C++，8 屏）。

客户端一律直接消费 protocol 的会话 API（FrameStream / ClipboardSession / FileSender / FileReceiver），只写 GUI 与传输绑定；协议库不写平台逻辑。旧 kde 客户端实现可从 git 历史（e8d205c / d1e71e4）找回。`CLAUDE.md` 保留设计期架构文档（双协议、五层模型仍有效），实现现状以本文件和实际代码为准。

### 目录地图
```
design/PROTOCOL.md        — 协议规范 v1.1（唯一权威，见原则 26）
design/unified/           — 统一设计系统（tokens.css/ui.css/app.js + screens/*.html，未来客户端 GUI 的唯一设计来源）
protocol/                 — Go 协议库（github.com/flowgrid/protocol），一个文件对应规范一节
protocol/session.go       — FrameStream：会话层发送接缝（seq/时钟/transport sink）
protocol/clipboard.go     — ClipboardSession：剪贴板收发会话（§5.3 分段/重组/超时）
protocol/filetransfer.go  — FileSender/FileReceiver：文件传输状态机（§7.4，SHA-256 校验）
protocol/cgo/bridge.go    — cgo 桥，构建产物 c-archive；句柄从 1 起，缓冲经 fg_free_buffer 释放
mac/                      — macOS 客户端（XcodeGen + SPM；构建入口与坑见 mac/README.md）
mac/FlowGridCore/         — SPM 包：CFlowGrid（cgo 头）/ FlowGridCore（域模型）/ FlowGridGo（FFI + 传输 + HAL）
mac/FlowGridUI/           — SPM 包：设计系统 / 图标引擎 / 组件 / 8 屏
windows/                  — Windows 客户端：fgcore（Go 会话核心，直引 protocol）+ FlowGrid.UI（WPF）；IPC 契约见 openspec/changes/windows-client/specs/windows-ipc
deepin/                   — deepin 客户端（DTK6 C++ Widgets，8 屏）
openspec/                 — 规格驱动开发：changes/archive/<日期-名称>/ + specs/
```

### 会话层链路（未来客户端的对接面）
- **发送**：客户端把传输 send 函数接成 `FrameSink` → `NewFrameStream` → `ClipboardSession.SendClipboard` / `FileSender.OfferFile` + `Pump`
- **接收**：传输收到的字节 → `DecodeFrameBytesWithHMAC` → `SequenceTracker.Check`（返回错误即丢帧，stale 帧不回 ERROR）→ 按 FrameType 分发：`CLIPBOARD` → `ClipboardSession.HandleFrame`；`FILE_*` → `FileReceiver.HandleFrame`

### 构建与测试
```bash
cd protocol && go test .                      # 协议根包（110+ 用例）
cd protocol && CGO_ENABLED=1 go test ./cgo    # cgo 桥（需 C 工具链）
cd windows && go test ./...                   # Windows 核心（IPC + mDNS 桥接）
dotnet build windows/ui                       # WPF UI
powershell -NoProfile -ExecutionPolicy Bypass -File windows/smoke.ps1
                                              # IPC 端到端冒烟（需先起 windows/fgcore.exe）
cd mac && make test                           # swift test：域逻辑 + FFI/会话 + 真实 TCP 链路集成
cd mac && make build                          # 构建 app（preBuildScript 自动按需编 c-archive）
```

### 当前检出环境
- **macOS 开发机（本检出，2026-10）**：`cd protocol && go test ./...`（含 cgo 子包，clang 可用）；`cd mac && make test && make build` 全绿。测试 runner 打开**外置卷**上的测试包会阻塞在 `open`（TCC 已放行也卡）→ mac 测试无宿主、派生数据放内置盘（`~/Library/Caches/FlowGrid/DerivedData`）；全屏截图受屏幕录制权限限制，窗口级 `screencapture -l <windowID>` 可用。
- **Windows 开发机（另一检出，2026-10）**：`cd protocol && go test .`、`cd windows && go test ./...`、`dotnet build windows/ui` 全绿；无 gcc 时 cgo 子包不可用（`CGO_ENABLED=0` 报 `undefined: registry`），装 MinGW-w64 后可跑且 c-archive 可用；该机直连 GitHub 不通时 origin 可临时指向 ghfast.top 镜像（**push 前必须**改回 `https://github.com/asdshuaishuai/flowgrid.git`）；Shell 为 CMD、GBK 乱码可用 `chcp 65001` 缓解。
- **通用**：新增/修改 cgo 导出后必须在 Linux 上重新生成 `cgo.h`（本机同版本 Go 的生成结果可作预检，逐字节一致）。

### 架构边界（改动的固定顺序）
wire format / 键码 / 握手有任何变动：**PROTOCOL.md → protocol/ Go 库 → protocol/cgo →（未来客户端）**。协议库不写平台专属逻辑——uinput / SendInput / IOKit / 传输加密（DTLS/TLS）都属于未来客户端的实现范围；协议库只做编解码、完整性与会话状态机。cgo 是未来 C/Rust 客户端的唯一 FFI 面：改 Go 导出签名必须同步 cgo.h。

### 工作流与 gotchas
- 功能变更走 OpenSpec 流程（propose → apply → archive → sync，命令模板在 `.opencode/commands/opsx-*.md`）；协议级变更归档示例见 `openspec/changes/archive/2026-10-16-protocol-v1-1/`
- 协议库改动的最小验证：`cd protocol && go test -count=1 . && go vet .`
- 新增协议文件后记得同步 PROTOCOL.md 目录映射与本文档的目录地图
- 旧客户端相关 gotchas（deepin DTK 规范、udev 规则、SNI 托盘、cxx-qt 等）已随代码移除，需要时从 git 历史 e8d205c / d1e71e4 / 6b9aafa 找回

---

*每条原则都对应着一个曾经踩过的坑。写下来，是为了以后不再踩。*  
*最后：对自己诚实，对代码诚实，对团队诚实。*
