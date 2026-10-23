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
输入 HAL 已是薄的（`input_engine` 只做 evdev 捕获/注入），**无内联重映射可删**；
捕获→对端的转发通路已补齐（见下）。

#### 能力接入：`deepin/src/core/protocolapi.{h,cpp}`
- `remapCapturedKey(hidKey, modifiers, from, to, keymap, context, out*)` —— 包 `fg_input_remap_key`，
  对应 mac 的 `GoProtocol.remapCapturedKey`；`keymap` 传 null = 只走平台默认。
- `class KeyMap` —— 键位规则句柄（`fg_keymap_load_file` / `fg_keymap_lookup` / `fg_keymap_free`），
  规则本体（YAML）在协议层解析，客户端只持句柄；加载失败时 `lastError()` 给出原因，
  **不静默按默认值跑**（那等于用户配的映射悄悄失效）。
- `identifyPayload()` / `identifyResponsePayload()` —— 握手载荷（§4.2）。
- 平台/角色/能力位常量（`platform::` / `role::` / `caps::`）镜像 `protocol/platform.go`，
  不各写各的字面量。

#### 转发通路：`deepin/src/core/input/input_forwarder.{h,cpp}`
- `Forwarder`：控制端 `forwardCaptured()` = 捕获事件 → **协议层重映射** → 载荷 → 发送回调；
  接收端 `handleInbound()` = 入站帧 → 解析 → HAL 注入。
  分工与 mac 的 `send?(type, payload)` / Windows 的 `SendToPeer(type, payload)` 一致：
  本层只产出「帧类型 + 载荷」，帧头由传输层补。
- `Injector` 抽象（在 `input_engine.h`）：生产环境是 `Engine` 本身，单测里是假实现——
  这样「入站帧 → 注入动作」的决策能脱离 `/dev/uinput` 权限被验证。

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
  `Engine` / `PeerSession` / `Forwarder`，界面只跟它打交道，不直接碰 socket。
- 连接屏：对端卡片带**「连接」/「断开」**按钮（形态跟真实连接状态走），
  顶部有**连接邀请条**（接受 / 拒绝），状态胶囊显示真实会话阶段
  （未连接 · 监听 34801 / 正在连接 X / 等待对方接受 / 已连接 X）。
- 接管开关真正生效：开 → `Engine::startCapture` + 转发；关 → 停捕获并把
  **还按着的键松开**（对端在另一台机器上，keyUp 不发出去那边的键会一直卡着）。
- 设备清单改用 `makeLanIdDeviceSource`（本机 + LAN 发现到的真实对端）；
  `DeviceManager` 现在**定期重新发现**（以前只在 setSource 时取一次，
  后来的对端永远不出现）。

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
| 真实注入（HAL） | `cd protocol && go build -o /tmp/fg-realinput ./cmd/realinput && /tmp/fg-realinput` | 协议 HID → `DecodeInputFrame` → cgo CoreGraphics 注入：`CGWarp` 真实移动光标（无需权限）、`CGEvent` 真实按键（需辅助功能权限）；带「读回光标位置」诊断 |

`realinput` 是 `//go:build darwin` 的最小 HAL 演示：能力（编/识）在协议层，注入在 HAL。

deepin 的两个单测**都不需要 `/dev/uinput` 权限**：注入侧用假 `Injector`，
会话侧用回环 socket（被控端与控制端都在本进程里）。真机权限只影响
「能不能真的注入」，不影响「逻辑对不对」——单测要验的是后者。

## 四、当前状态
- 协议层：`DecodeInputFrame` / `RemapCapturedKey` + 单测，`fg_input_remap_key` + 双头同步 —— 完成。
- mac：发送侧重映射走协议层，删 Swift 重复实现，`swift test` 62 绿 —— 完成。
- Windows：`hub.go` 收敛到 `RemapCapturedKey`，`GOOS=windows go build` 通过 —— 完成。
- deepin：能力接入 + 转发通路 + 对端会话 + 界面接线 + 四个单测 —— 完成；
  真机实测已跑通「发现 → 连接 → 握手 → 已连接 → 接管开关」。
  evdev/uinput 那一层的真机单测需要 root 权限（见「三、测试」）。

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
4. **deepin 的键位规则接线**：`proto::KeyMap` 句柄与 `Forwarder::setKeyMap()`
   都已备好，但 deepin 客户端还没把自己的 `keymap.yaml` 加载进来——
   与 mac 的后续项 1 是同一件事。
5. 接收侧若未来改为「发原始键、收端重映射」，收端即用 `DecodeInputFrame(from=对端, to=本机)`；
   当前 mac 与 deepin 都采用「发前重映射」，收端只解析注入（勿二次重映射）。
