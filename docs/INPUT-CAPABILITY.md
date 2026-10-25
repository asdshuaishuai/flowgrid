# 输入能力：协议层实现 + 客户端薄 HAL（设计 / 实现 / 测试）

> 对应原则：AGENTS.md **§29 能力在协议层，客户端只做 UI + HAL**。
> 键鼠的「识别 + 跨平台重映射」是**协议层能力**；各客户端只做 OS HAL（捕获/注入）、
> 传输、UI 与键位映射的可视化配置，**禁止在客户端重写这套逻辑**。

## 一、设计

### 为什么
键鼠操作跨 macOS / Windows / Linux 时，修饰键要互换（§6.3：Mac 的 ⌘Cmd ⇄ Win/Linux 的
Ctrl），用户还能自定义键位规则（§11）。这套「识别一帧输入 → 按目标平台重映射」的逻辑必须在
**一处**实现：否则每个客户端各写一遍，就会行为分叉 + 重复维护（DRY 的项目实例）。

### 两个入口（客户端只调这两个，勿自行 decode/remap）
| 方向 | 入口 | 作用 |
| --- | --- | --- |
| 接收 | `protocol.DecodeInputFrame(ft, payload, from, to, table, ctx)` | 把一条 `KEY_*/MOUSE_*/SCROLL` 帧解码 + 按 `from→to` 重映射成 `InputEvent`；客户端只做 `InputEvent → OS 注入` |
| 发送 | `protocol.RemapCapturedKey(hidKey, modifiers, from, to, table, ctx)` | 把本机捕获的键（HID Usage + 修饰掩码）重映射成目标平台的 (键码, 掩码)；客户端组帧后发送 |

`InputEvent`（`protocol/input.go`）用 `Type` 判别（`FrameKeyDown/KeyUp/MouseMove/MouseButton/Scroll`），
带重映射后的 `KeyCode/Modifiers`、`DeltaX/DeltaY`、`Button/Pressed`，以及溯源用的
`FromPlatform/ToPlatform/OrigKeyCode/OrigModifiers/Remapped`；`Describe()` 给一行人类可读摘要（日志/UI）。

### 重映射语义（`RemapCapturedKey` / `DecodeInputFrame` 共用）
1. `from == to`：原样返回（no-op）。
2. **掩码先走** `KeyMapTable.RemapModifierMask`（规则感知：每个修饰键先查用户 keymap 规则，
   未命中走平台默认；`table` 为 `nil` 时退化为平台默认）——保证「掩码与键一致」，
   避免「按的是 Alt、掩码说 Ctrl」。
3. **键再走** `table.Lookup`（显式规则优先，用已换算的掩码匹配），未命中回退
   `RemapModifierKey` + 平台默认 Ctrl⇄Meta（§6.3）。
4. 鼠标移动/滚轮无键义，直通。

## 二、实现

### 协议层（唯一真相源）
- `protocol/input.go`：`InputEvent` + `DecodeInputFrame` + `RemapCapturedKey` + `Describe`。
  纯逻辑、零平台专属代码；只组合既有原语（`KeyMapTable.Lookup` / `RemapModifierMask` /
  `RemapModifierKey` / `RemapModifiers` / `ModifierFromSingleKey`）。
- `protocol/cgo/bridge.go`：导出 `fg_input_remap_key`，包装 `RemapCapturedKey`
  （`tableHandle` 用与 `fg_keymap_lookup` 相同的句柄，0 = nil；`context` 为 `char*`，空 = global）。

### 头文件同步（改导出必做，见 AGENTS.md）
- `protocol/cgo/cgo.h`：C/Rust 客户端的规范 FFI 头，**改导出后在 Linux 重新生成**。
- `mac/FlowGridCore/Sources/CFlowGrid/include/flowgrid.h`：`mac/scripts/build-go.sh` 由
  c-archive 刷新。**macOS 重建会剥掉 Go 文档注释**——不要提交剥壳版：手工只在
  `fg_remap_modifiers` 后加一行 `fg_input_remap_key` 声明（与 Linux 生成结果一致）。
- `mac/Vendor/libflowgrid.a`：gitignore，按平台重建。

### mac 客户端（`mac/FlowGridCore`）
- `FlowGridGo/GoProtocol.swift`：`remapCapturedKey(_:modifiers:from:to:)` 包装 `fg_input_remap_key`。
- `FlowGridGo/HAL/InputCaptureTap.swift`：`keyDown/keyUp/flagsChanged` 三个分支由原来的
  「`KeyCodeTable.remapModifierKey` + `GoProtocol.remapModifiers` 两处」改为**一次** `remapCapturedKey`。
- **删除** `KeyCodeTable.remapModifierKey` 与 `usesMetaModifier`——它们正是 Swift 重写协议
  `RemapModifierKey` 的重复实现。`KeyCodeTable` 只留 macOS 虚拟键码 ↔ HID Usage 的 HAL 翻译表。
- 测试改用协议路径：`Tests/FlowGridGoTests/InputMappingTests.swift` 的 `remapCapturedKeyAcrossPlatforms`。

### Windows 客户端（`windows`）
- `internal/hub/hub.go` `forward()`：把发送侧重映射的 if/else 内联编排
  （`RemapModifierMask`+`Lookup`+`RemapModifierKey` / else `RemapModifiers`+`RemapModifierKey`）
  改为**一次** `protocol.RemapCapturedKey(ev.HID, ev.Modifiers, PlatformWindows, peerPlatform, h.keymap, ctx)`
  （逐行等价；`ctx` 仅在有 keymap 时算，保留无 keymap 零开销）。

### deepin 客户端（`deepin`）
**已迁到 oscontrol**（2026-10-25）：C++ 侧的 `input_engine` / `input_forwarder` /
`input_keymap.h` 全部删除，OS 机制独家归协议层的 `protocol/oscontrol/evdev`。
deepin 只剩一个薄壳 `src/core/input/oscontrol.{h,cpp}`（起停捕获线程、取帧、
递帧给注入），**客户端里不再有任何键鼠语义**。

#### 能力接入：`deepin/src/core/protocolapi.{h,cpp}`
- `remapCapturedKey(hidKey, modifiers, from, to, keymap, context, out*)` —— 包 `fg_input_remap_key`，
  对应 mac 的 `GoProtocol.remapCapturedKey`；`keymap` 传 null = 只走平台默认。
- `class KeyMap` —— 键位规则句柄（`fg_keymap_load_file` / `fg_keymap_lookup` / `fg_keymap_free`），
  规则本体（YAML）在协议层解析，客户端只持句柄；加载失败时 `lastError()` 给出原因，
  **不静默按默认值跑**（那等于用户配的映射悄悄失效）。
- `identifyPayload()` / `identifyResponsePayload()` —— 握手载荷（§4.2）。
- 平台/角色/能力位常量（`platform::` / `role::` / `caps::`）镜像 `protocol/platform.go`，
  不各写各的字面量。

#### OS 控制层薄壳：`deepin/src/core/input/oscontrol.{h,cpp}`
（替代原来的 `input_forwarder` + `input_engine`；2026-10-25）
- `OsControl` 只做三件事：起停捕获线程、把产出的**成品帧**交给上层、把入站帧
  递给协议层注入。**它不写键位换算、不拼载荷、不管修饰键合成**——那些在
  协议层（`protocol/oscontrol` + `protocol/oscontrol/evdev`）。
- 控制端：`startCapture()` → 协议层把「捕获 → 重映射 → 组帧」走完 → 帧从出口
  队列取出来 → `PeerSession::sendFrame()`。
- 停接管：`stopCapture()` 先停线程，再让协议层把**还按着的键松开**（keyUp 帧
  照样送出去）——对端在另一台机器上，不发松开帧那边的键会一直卡着。
- 被控端：`injectFrame()` 把入站帧递进协议层（解码 → 注入）。剪贴板、文件这些
  不是输入帧，注入会失败——那是正常情况，不算错。
- 客户端**不再自己挑帧类型、自己拼载荷、自己调重映射**——那段语义以前在 deepin 与
  mac 各写一份，正是行为分叉的根源；现在由协议层独占，跨端一致性由黄金向量钉住。

#### cgo 桥：`protocol/cgo/oscontrol_linux.go`
（2026-10-25 新增；非 Linux 平台有同名桩 `oscontrol_other.go`）

- **为什么是「拉取」而不是「回调」**：控制端「停接管时把还按着的键松开」必须
  **回发 keyUp 帧**，也就是捕获侧本身要有发帧的出口。回调模型下这个出口是
  C 侧给的函数指针——而 **cgo 不支持 Go 去调一个 C 传来的函数指针**（typedef
  出来的函数指针在 Go 侧是不透明类型，`C.fg_oscontrol_sink_t` 只能当指针存，
  调不了）。拉取模型把这个出口做成 Go 侧的队列：C 侧取帧、送上网线；松键时
  协议层把 keyUp 帧塞进同一个队列，C 侧照样取得到。语义一样，少一层 FFI 魔法。
- 队列容量 256，**满了不丢帧**：发送方等。捕获侧慢下来是背压，不是丢事件——
  丢一个 mouse_move 无所谓，丢一个 keyDown 就等于对端少按了一次键。
- `fg_oscontrol_set_peer` 必须在 `fg_oscontrol_start_capture` **之前**调：
  重映射的方向取决于对端平台（§6.3）。

#### 对端会话：`deepin/src/core/input/peer_session.{h,cpp}`
- `PeerSession`：QTcpSocket + **§7.2 长度前缀**（Length 2B BE + HID Frame [+8B HMAC]）+
  IDENTIFY 握手（§4.2：发起方发 IDENTIFY，被控端弹邀请，接受/拒绝后回 IDENTIFY_RESPONSE）。
- 会话 HMAC 密钥由**配对码派生**，算法与 mac 的 `SessionKey.derive` 一致：
  `SHA256("FlowGrid-mvp-session|" + 归一化码)`。两端算不出同一把密钥时收不到对方的帧，
  症状是「连上了却一条帧都收不到」——所以这步不能省，也不能各自发挥。
- 会话端口 `kPeerSessionPort = 34801`：**不能**与 LAN ID 的组播 UDP 端口 24801 相同。
  通告里报的就是这个端口（对端据此 dial），与 web demo 的做法一致；
  mac 目前通告的仍是 24801（UDP 端口，拨不通），等 mac 侧也改成报会话端口才能互通。

#### 界面接线：`core/peercontroller.{h,cpp}` + `ui/connectionspanel.{h,cpp}`
- `PeerController`（单例）把「发现 → 连接 → 捕获/注入」串成一条链：持有
  `OsControl` / `PeerSession`，界面只跟它打交道，不直接碰 socket。
- 连接屏：对端卡片带**「连接」/「断开」**按钮（形态跟真实连接状态走），
  顶部有**连接邀请条**（接受 / 拒绝），状态胶囊显示真实会话阶段
  （未连接 · 监听 34801 / 正在连接 X / 等待对方接受 / 已连接 X）。
- 接管开关真正生效：开 → `OsControl::startCapture` + 转发；关 → 停捕获并把
  **还按着的键松开**（对端在另一台机器上，keyUp 不发出去那边的键会一直卡着）。
- 设备清单改用 `makeLanIdDeviceSource`（本机 + LAN 发现到的真实对端）；
  `DeviceManager` 现在**定期重新发现**（以前只在 setSource 时取一次，
  后来的对端永远不出现）。

#### 规范化的输入模型 + 对称编解码：`protocol/inputevent.go`

各平台原本各自定义「捕获到的事件」——deepin 是 `EventKind + pressed 位 + deltaX/deltaY`，
mac 是 CGEvent 原生字段（`virtualKey` / `flagsRaw`）。于是「修饰键怎么表达」
「水平滚轮哪边算正」「按下与松开怎么区分」这些**协议语义**被各端各写一遍，迟早分叉。

现在协议层有一份规范模型 `protocol.Input`（Kind / Key / Modifiers / DeltaX / DeltaY /
Button / Pressed），外加两个对称函数：

- `EncodeInput(in, from, to, table, ctx)` → (帧类型, 载荷)：**发前重映射**（§6.3）。
- `DecodeInput(frameType, payload)` → `Input`：收端**不二次重映射**。

`InputKind` 与帧类型一一对应且同值（`key_down` ↔ `KEY_DOWN` 0x01 …），客户端因此
不必再镜像一套「操作类型」常量。

#### OS 控制抽象层：`protocol/oscontrol`

「操作系统输入控制」的契约层，**依赖倒置**——协议层不 import 任何平台：

```go
type Controller interface {
    Platform() protocol.Platform
    Capture() (protocol.Input, error)   // 阻塞直到捕获到一个事件
    Inject(in protocol.Input) error
    ReleasePressed() error              // 松开还按着的键（对端走了不会自动清）
    Close() error
}
oscontrol.Register(platform, factory)  // 各平台注册自己的实现
oscontrol.For(platform)                // 协议层按 os 类型取，不关心实现
```

`oscontrol.Forwarder` 把「捕获 → 协议层重映射并编码 → 交给 sink」收进协议层；
`oscontrol.Injector` 把「入站帧 → 解码 → 注入」收进协议层。客户端只剩三件事：
提供 `Controller`、提供传输、画界面。

Linux 实现在 `protocol/oscontrol/evdev`（evdev 捕获 + uinput 注入，行为对齐现有
C++ 引擎）。其它平台照同一份契约实现即可。

**踩过的坑（写下来免得再踩）**：
- `UI_DEV_CREATE` 是 `_IO`（无 size 字段），不是 `_IOW`——写成 `_IOW` 编码内核直接
  回 EINVAL，而前面的 `UI_SET_*` 照样成功，症状极难查。
- `EVIOCGBIT(ev,len)` 的 `nr` 字段是 **`ev + 0x20`**，不是 `ev`：写错的话命令编码
  对不上，内核同样回 EINVAL（而 `EVIOCGNAME` 不受影响，看着像权限问题）。
- `EVIOCGBIT(0, len)` 才是「支持哪些事件类型」，`EVIOCGBIT(EV_KEY, len)` 是键码位图。

#### 跨客户端黄金向量：`protocol/testdata/input_vectors.json`

「某个输入事件在协议层应该变成什么字节」只有协议层知道。以前这条只靠各端单测里
零散的断言守着——两端各写各的期望值，行为迟早分叉，而且分叉了也没人知道是哪端错了。

现在协议层用**自己的函数**（`EncodeInput`）把期望值算出来，落到一份 JSON：

- `constants`：帧类型 / 平台 / 修饰位 / 角色 / 能力位 / 鼠标按钮号——
  客户端代码里手抄了这些常量，现在逐个跟这份表比对，协议层改了而客户端忘了改立刻红。
- `cases`：`from_platform` → `to_platform` 的输入事件序列，以及期望的线上字节。

各端测试读**同一份文件**：deepin 侧是 `deepin/tests/input_vector_test.cpp`
（只跑 `from_platform=linux` 的用例，其余留给 mac / Windows 客户端）。

维护：改完协议行为跑
`cd protocol && go run ./cmd/inputvectors -w testdata/input_vectors.json`；
协议层有测试守着这份文件与生成器输出一致（忘了重新生成会被 CI 逮住）。

#### web demo 的分帧对齐（§7.2）
demo 原先自己手写了一套 **4 字节**长度前缀，而协议 §7.2 与 mac / deepin 都是
**2 字节** —— 两端对不上帧长，demo 与真客户端直接互通不了。已改成 2 字节。
demo 的会话 HMAC 密钥也改成**由配对码派生**（算法同 mac 的 `SessionKey.derive`），
不再是一把写死的密钥（写死的只有 demo 自己知道，跟真客户端永远对不上）。
联调时用 `FLOWGRID_DEMO_PAIRING=<对端配对码>` 让两端算出同一把密钥。

**真机实测**（deepin 客户端 ↔ web demo，两个进程、真实 TCP）：
发现对端 → 点「连接」→ demo 侧验签通过并生成邀请 → 接受 → 双方 `已连接`。

## 三、测试

| 层 | 命令 / 方式 | 覆盖 |
| --- | --- | --- |
| 协议单测 | `cd protocol && go test -run 'Input|Remap|DecodeInputFrame' -v .` | `input_test.go`：5 类帧、Cmd+C mac→linux=Ctrl+C、nil table 默认、规则覆盖、同平台 no-op、坏 payload/未知帧 error、`Describe` |
| cgo FFI | 建 c-archive 后写 C `main` 调 `fg_input_remap_key` 编译链接运行 | C 标量进 / out 参数出 / 句柄 0 / 跨平台重映射结果 |
| mac | `cd mac/FlowGridCore && swift test` | `remapCapturedKeyAcrossPlatforms`（证明 mac 重映射走协议层）；全量 62 绿 |
| Windows | Windows 机上 `cd windows && go test ./...`；任意机 `cd windows && GOOS=windows go build ./...` | hub 收敛后编译/回归 |
| deepin 转发通路 | `./deepin/dev.sh --check`（或 `flowgrid-inputforwardtest`） | 跨平台重映射（Linux Ctrl→macOS Meta）、KEY_UP 键码仍重映射、鼠标按下/松开可区分、滚轮 delta 不错位、入站帧分发到假 Injector、非输入帧不误注入 |
| deepin 对端会话 | `flowgrid-peersessiontest` | 回环 TCP 上完成 IDENTIFY 握手（接受/拒绝两条路）、业务帧往返载荷一致、配对码不一致时帧被丢弃 |
| 跨端黄金向量 | `flowgrid-inputvectortest`（`dev.sh --check` 里也跑）；协议层：`go test -run InputVector .` | 客户端镜像的常量（帧类型/平台/修饰位/角色/能力位/按钮号）逐个对着协议层真值核对；转发通路产出的载荷字节对着向量文件核对 |
| OS 控制抽象层 | `cd protocol && go test ./oscontrol/...` | 契约注册表（没注册就报 `ErrNotSupported`，不返回 nil）；流水线「捕获 → 编码 → 帧」「帧 → 解码 → 注入」；Linux 实现的 HID↔evdev 表往返、修饰键映射、滚轮方向换算 |
| deepin HAL 真机 | `flowgrid-inputhaltest`（`dev.sh --check` 里也跑） | 真实 evdev 捕获 → 协议重映射 → 真实 TCP → 对端解析 → 真实 uinput 注入 → **读回虚拟设备的事件流**逐条核对 → **X 光标位移**（见下） |
| 真实注入（HAL） | `cd protocol && go build -o /tmp/fg-realinput ./cmd/realinput && /tmp/fg-realinput` | 协议 HID → `DecodeInputFrame` → cgo CoreGraphics 注入：`CGWarp` 真实移动光标（无需权限）、`CGEvent` 真实按键（需辅助功能权限）；带「读回光标位置」诊断 |

`realinput` 是 `//go:build darwin` 的最小 HAL 演示：能力（编/识）在协议层，注入在 HAL。

deepin 的两个单测**都不需要 `/dev/uinput` 权限**：注入侧用假 `Injector`，
会话侧用回环 socket（被控端与控制端都在本进程里）。真机权限只影响
「能不能真的注入」，不影响「逻辑对不对」——单测要验的是后者。

### deepin HAL 真机测试：怎么算「真的动了」

`flowgrid-inputhaltest` 一次跑三件事，缺哪件就如实说缺哪件（退出码 77 = skip）：

1. **捕获侧**：用 uinput 造一个名叫 `FlowGrid HAL Test Device` 的「物理键鼠」
   （名字**故意**与引擎自己的 `FlowGrid Virtual Input` 不同，否则会被捕获引擎
   按名字跳过），从它发出真实的 `REL_X/REL_Y` 与 `KEY_A`；捕获引擎从
   `/dev/input/event*` 读到这些事件——走的是生产用的那条代码路径。
   合成设备**故意不声明 `BTN_LEFT`**：X 只把带按钮的设备认成指针，不声明它
   就不会抢走桌面的光标，光标的位移因此能干净地归因到注入通路上。
2. **注入侧读回**：把 `FlowGrid Virtual Input` 的 evdev 流读回来，逐条核对
   `REL_X=+120 / REL_Y=-60 / KEY_A down+up` 真的落进了 uinput——不是
   「注入调用没报错」就算过。
3. **X 光标位移**：注入前后各读一次 X 光标位置，断言光标确实往右、往上动了。
   测量前会 `EVIOCGRAB` 接管真实鼠标（**排除引擎自己的虚拟设备**，否则 X 就
   收不到我们注入的事件），这样光标的位移只可能来自注入。
   X 的指针加速会缩放位移（实测注入 120 → 光标走 103），所以只判方向与
   「确实动了」，不判精确值。

实测（deepin 真机，注入 `dx=+120 dy=-60`）：

```
[HAL] 已接管 1 个真实鼠标设备（光标位移只剩注入这一条来源）
[HAL] 注入前 X 光标：(763,486)
[HAL] X 光标：(763,486) → (867,434)  Δ=(+104,-52)
```

**权限的现实**：`/dev/uinput` 的写权限由 `99-flowgrid-input.rules` 的 uaccess
给到当前会话用户；但 **uinput 造出来的虚拟设备节点（`root:input 0660`）不在
uaccess 的覆盖范围内**——虚拟设备没有物理路径，systemd 分不出它属于哪个
seat，uaccess builtin 会跳过它。结果是：写得到、读不回来。要把捕获侧与
读回验证也跑通，需要本用户在 `input` 组里：

```bash
sudo usermod -aG input $USER     # 一次性；之后重新登录，或用 sg input -c '...' 直接跑
```

权限不全时测试**降级但不假装**：捕获侧与读回验证不可用就退到「只跑注入侧」
（协议 → TCP → 注入照样走完，光标照样动），并明确说明哪几段没验。


## 四、当前状态
- 协议层：`DecodeInputFrame` / `RemapCapturedKey` + 单测，`fg_input_remap_key` + 双头同步 —— 完成。
- mac：发送侧重映射走协议层，删 Swift 重复实现，`swift test` 62 绿 —— 完成。
- Windows：`hub.go` 收敛到 `RemapCapturedKey`，`GOOS=windows go build` 通过 —— 完成。
- deepin：能力接入 + 转发通路 + 对端会话 + 界面接线 + 六个单测 —— 完成；
  真机实测已跑通「发现 → 连接 → 握手 → 已连接 → 接管开关」。
  HAL 真机测试已跑通「真实 evdev 捕获 → 协议 → TCP → uinput 注入 → X 光标位移」，
  其中读回验证与合成设备捕获还差 `input` 组权限（见「三、测试」）。
  转发通路已改走协议层的 `EncodeInput`：客户端不再自己挑帧类型、自己拼载荷。
  键位规则已接线：规则文件按用户级 → 系统级 → 应用自带的顺序加载，连接时交给
  重映射通路，界面显示的是**同一张表**（详见「五、后续」第 4 条）。
- OS 控制抽象层：`protocol/oscontrol`（契约 + 注册表 + 流水线）与 Linux 实现
  `protocol/oscontrol/evdev` —— 完成，Go 单测全绿（真机那段因同样的权限缺口跳过）。

## 五、后续（follow-up）
1. **mac 捕获路径接用户 keymap 规则**：目前 `InputCaptureTap` 传 `nil` table（只走平台默认
   Ctrl⇄Meta），而 Windows 已传 `h.keymap`。要接上需：持有一个长生命周期 `GoKeyMap` 句柄 →
   经 `PeerSession` 透传给 `InputCaptureTap` → `GoProtocol.remapCapturedKey` 增 `tableHandle` 形参
   透传 `fg_input_remap_key`（并处理 keymap 热重载时的句柄更新）。
   deepin 侧已备好 `proto::KeyMap` 句柄，`Forwarder::setKeyMap()` 可直接注入。
2. **协议层补 IDENTIFY 反序列化导出**：cgo 侧只有 `fg_identify_payload_encode` /
   `fg_identify_response_encode`，没有对应的 unmarshal 导出，所以 deepin 的 `PeerSession`
   拿不到对端在 IDENTIFY 里报的名字（现在用 socket 的对端地址兜底）。
   要补就得动 `protocol/cgo/bridge.go` + `cgo.h`，**并按 AGENTS.md 同步
   `mac/FlowGridCore/Sources/CFlowGrid/include/flowgrid.h`**（macOS 重建会剥掉 Go 文档注释，
   不要提交剥壳版）。
3. **deepin 与 mac 的 LAN ID 互通**：deepin 的通告已改报会话端口 34801，
   但 mac 侧仍通告 24801（UDP 端口，拨不通）。mac 侧也改成报会话端口后，
   两端才能仅靠 LAN ID 发现就连上（现在 mac 走的是 DirectLinkManager 的
   BLE / Wi-Fi Direct / mDNS 通路）。
4. ~~**deepin 的键位规则接线**~~ —— **已完成（2026-10-25）**。
   deepin 客户端按 `~/.config/flowgrid/keymap.yaml` → `/usr/local/share/flowgrid/keymap.yaml`
   → `/usr/share/flowgrid/keymap.yaml` → 应用自带的 `:/keymap.yaml` 的顺序加载规则，
   连接时把**同一张表**交给 `OsControl::setPeer()`，规则因此真的参与重映射。
   键位映射屏显示的全局网格 / 应用覆盖 / 冲突横幅也全部来自这张表（原先是写死的
   示例数据），`flowgrid-uitest` 的「键位映射」一段逐条比对二者。
   **未做的部分**：应用级规则（`app:` / `game` 上下文）只解析、不参与换算——
   重映射通路固定按全局规则查，还没有「前台应用 → context」的判定。
5. 接收侧若未来改为「发原始键、收端重映射」，收端即用 `DecodeInputFrame(from=对端, to=本机)`；
   当前 mac 与 deepin 都采用「发前重映射」，收端只解析注入（勿二次重映射）。
6. ~~**deepin 客户端迁到 Go 的 oscontrol**~~ —— **已完成（2026-10-25）**。
   cgo 桥在 `protocol/cgo/oscontrol_linux.go`（非 Linux 有同名桩），deepin 侧的
   `input_engine` / `input_forwarder` / `input_keymap.h` 已删除，只剩
   `src/core/input/oscontrol.{h,cpp}` 薄壳 + 传输 + 界面。
   迁移期由黄金向量钉住：向量文件重生成后字节数不变（6034），行为等价。
7. **mac 侧的 oscontrol 实现**：把 `InputCaptureTap` / `InputInjector` 按
   `oscontrol.Controller` 契约重写并注册，mac 侧也就能读同一份黄金向量文件。
8. **uinput 虚拟设备的 uaccess 覆盖**：虚拟设备没有物理路径，systemd 的 uaccess
   builtin 分不出它属于哪个 seat，不会给它发 ACL——节点停在 `root:input 0660`，
   「写得进去、读不回来」。现在只影响 HAL 测试的读回验证与合成设备捕获
   （产品链路本身不需要读自己的虚拟设备：写 `/dev/uinput` 有权限即可）。
   若将来产品要读回自己造的虚拟设备，两条路：让用户在 `input` 组里，或在
   udev 规则里给虚拟设备显式指定 seat（`ENV{ID_SEAT}="seat0"`）。
