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
捕获→对端的转发通路待补（见「五、后续」）。

## 三、测试

| 层 | 命令 / 方式 | 覆盖 |
| --- | --- | --- |
| 协议单测 | `cd protocol && go test -run 'Input|Remap|DecodeInputFrame' -v .` | `input_test.go`：5 类帧、Cmd+C mac→linux=Ctrl+C、nil table 默认、规则覆盖、同平台 no-op、坏 payload/未知帧 error、`Describe` |
| cgo FFI | 建 c-archive 后写 C `main` 调 `fg_input_remap_key` 编译链接运行 | C 标量进 / out 参数出 / 句柄 0 / 跨平台重映射结果 |
| mac | `cd mac/FlowGridCore && swift test` | `remapCapturedKeyAcrossPlatforms`（证明 mac 重映射走协议层）；全量 62 绿 |
| Windows | Windows 机上 `cd windows && go test ./...`；任意机 `cd windows && GOOS=windows go build ./...` | hub 收敛后编译/回归 |
| 真实注入（HAL） | `cd protocol && go build -o /tmp/fg-realinput ./cmd/realinput && /tmp/fg-realinput` | 协议 HID → `DecodeInputFrame` → cgo CoreGraphics 注入：`CGWarp` 真实移动光标（无需权限）、`CGEvent` 真实按键（需辅助功能权限）；带「读回光标位置」诊断 |

`realinput` 是 `//go:build darwin` 的最小 HAL 演示：能力（编/识）在协议层，注入在 HAL。

## 四、当前状态
- 协议层：`DecodeInputFrame` / `RemapCapturedKey` + 单测，`fg_input_remap_key` + 双头同步 —— 完成。
- mac：发送侧重映射走协议层，删 Swift 重复实现，`swift test` 62 绿 —— 完成。
- Windows：`hub.go` 收敛到 `RemapCapturedKey`，`GOOS=windows go build` 通过 —— 完成。
- deepin：无重复可删；转发通路待补。

## 五、后续（follow-up）
1. **mac 捕获路径接用户 keymap 规则**：目前 `InputCaptureTap` 传 `nil` table（只走平台默认
   Ctrl⇄Meta），而 Windows 已传 `h.keymap`。要接上需：持有一个长生命周期 `GoKeyMap` 句柄 →
   经 `PeerSession` 透传给 `InputCaptureTap` → `GoProtocol.remapCapturedKey` 增 `tableHandle` 形参
   透传 `fg_input_remap_key`（并处理 keymap 热重载时的句柄更新）。
2. **deepin 转发通路**：把 `input_engine` 捕获的 `CapturedEvent` 经 cgo `fg_input_remap_key`
   重映射 + 组帧发送，客户端仍只留 evdev HAL + UI。
3. 接收侧若未来改为「发原始键、收端重映射」，收端即用 `DecodeInputFrame(from=对端, to=本机)`；
   当前 mac 采用「发前重映射」，收端只解析注入（勿二次重映射）。
