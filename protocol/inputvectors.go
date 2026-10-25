package protocol

// 输入行为黄金向量（跨客户端共用）。
//
// 为什么要有它：mac / Windows / deepin 各自实现捕获与注入的 HAL，但
// 「某个输入事件在协议层应该变成什么字节」只有协议层知道。以前这条只靠
// 各端单测里的零散断言守着——两端各写各的期望值，行为迟早分叉，而且
// 分叉了也没人知道是哪端错了。
//
// 这里的向量由协议层自己算出来（RemapCapturedKey + 各载荷的 Marshal），
// 落到 testdata/input_vectors.json；各端测试读**同一份文件**，把自己
// 产出的字节与文件比对——「两端行为一致」从此不靠人盯。
//
// 维护：改完协议行为后重新生成——
//     cd protocol && go run ./cmd/inputvectors -w testdata/input_vectors.json
// 协议层有测试守着这份文件与生成器输出是否一致（input_vectors_test.go），
// 所以「忘了重新生成」会被 CI 逮住。

import (
	"encoding/hex"
	"fmt"
)

// inputVectorsVersion 是向量文件本身的格式版本。
// 改动向量文件的结构（而不是向量内容）时 +1，客户端据此拒绝不认识的文件。
const inputVectorsVersion = 1

// VectorEvent 是向量里的一个输入事件。
//
// 字段与各端 HAL 的「捕获到的事件」一一对应，但只用协议层认识的量：
// key 是 HID Usage ID（§6.1），modifiers 是 §6.2 的位掩码。
type VectorEvent struct {
	Kind      string `json:"kind"` // key_down / key_up / mouse_move / mouse_button / scroll
	Key       uint16 `json:"key,omitempty"`
	Modifiers uint8  `json:"modifiers,omitempty"`
	DeltaX    int16  `json:"delta_x,omitempty"`
	DeltaY    int16  `json:"delta_y,omitempty"`
	Button    uint8  `json:"button,omitempty"`
	Pressed   bool   `json:"pressed,omitempty"`
}

// VectorExpect 是这个事件在**线上**应该长成什么样。
//
// PayloadHex 是载荷的十六进制（不含帧头与 HMAC，§5.3 的载荷本体）。
type VectorExpect struct {
	FrameType  string `json:"frame_type"`
	Key        uint16 `json:"key,omitempty"`
	Modifiers  uint8  `json:"modifiers,omitempty"`
	DeltaX     int16  `json:"delta_x,omitempty"`
	DeltaY     int16  `json:"delta_y,omitempty"`
	Button     uint8  `json:"button,omitempty"`
	Pressed    bool   `json:"pressed,omitempty"`
	PayloadHex string `json:"payload_hex"`
}

// VectorCase 是一组「输入事件 → 期望线上帧」的用例。
//
// From / To 是平台名（见 platformKey）。客户端只跑 from 等于自己平台的用例——
// 同一份文件因此能同时钉住 mac（from=macos）与 deepin（from=linux）。
type VectorCase struct {
	Name   string         `json:"name"`
	From   string         `json:"from_platform"`
	To     string         `json:"to_platform"`
	Events []VectorEvent  `json:"events"`
	Expect []VectorExpect `json:"expect"`
}

// VectorConstants 是协议层的常量表。
//
// 放进向量文件的目的很实际：各端客户端在自己的代码里镜像了这些常量
// （帧类型、平台、修饰位），以前没有任何机制保证镜像的值跟协议层一致。
// 现在客户端测试会拿镜像值跟这份表逐个比对，协议层改了值而客户端忘了改，
// 立刻就有测试红。
type VectorConstants struct {
	ProtocolVersion int               `json:"protocol_version"`
	FrameTypes      map[string]uint8  `json:"frame_types"`
	Platforms       map[string]uint8  `json:"platforms"`
	Modifiers       map[string]uint8  `json:"modifiers"`
	Roles           map[string]uint8  `json:"roles"`
	Capabilities    map[string]uint8  `json:"capabilities"`
	MouseButtons    map[string]uint8  `json:"mouse_buttons"`
}

// InputVectors 是向量文件的顶层结构。
type InputVectors struct {
	Version   int             `json:"version"`
	Note      string          `json:"note"`
	Constants VectorConstants `json:"constants"`
	Cases     []VectorCase    `json:"cases"`
}

// platformKey 把平台枚举变成向量文件里用的稳定名字。
func platformKey(p Platform) string {
	switch p {
	case PlatformMacOS:
		return "macos"
	case PlatformWindows:
		return "windows"
	case PlatformLinux:
		return "linux"
	case PlatformAndroid:
		return "android"
	case PlatformIOS:
		return "ios"
	default:
		return fmt.Sprintf("unknown_0x%02x", uint8(p))
	}
}

// BuildInputVectors 算出全套向量。
//
// 期望值不是手写的：全部由协议层自己的函数算出（RemapCapturedKey + 载荷
// 的 Marshal），所以这份文件永远是「协议层现在认为的正确行为」。
func BuildInputVectors() *InputVectors {
	v := &InputVectors{
		Version: inputVectorsVersion,
		Note: "协议层生成的跨客户端输入行为黄金向量。各端测试读这份文件，" +
			"把自己产出的字节与这里比对；改了协议行为请跑 " +
			"`go run ./cmd/inputvectors -w testdata/input_vectors.json` 重新生成。",
		Constants: VectorConstants{
			ProtocolVersion: int(ProtocolVersion),
			FrameTypes: map[string]uint8{
				"key_down":       FrameKeyDown,
				"key_up":         FrameKeyUp,
				"mouse_move":     FrameMouseMove,
				"mouse_button":   FrameMouseButton,
				"scroll":         FrameScroll,
				"clipboard":      FrameClipboard,
				"heartbeat":      FrameHeartbeat,
				"latency_ping":   FrameLatencyPing,
				"latency_pong":   FrameLatencyPong,
				"identify":       FrameIdentify,
				"identify_resp":  FrameIdentifyRes,
			},
			Platforms: map[string]uint8{
				"macos":   uint8(PlatformMacOS),
				"windows": uint8(PlatformWindows),
				"linux":   uint8(PlatformLinux),
				"android": uint8(PlatformAndroid),
				"ios":     uint8(PlatformIOS),
			},
			Modifiers: map[string]uint8{
				"left_ctrl":   ModLeftCtrl,
				"left_shift":  ModLeftShift,
				"left_alt":    ModLeftAlt,
				"left_meta":   ModLeftMeta,
				"right_ctrl":  ModRightCtrl,
				"right_shift": ModRightShift,
				"right_alt":   ModRightAlt,
				"right_meta":  ModRightMeta,
				"ctrl":        ModCtrl,
				"shift":       ModShift,
				"alt":         ModAlt,
				"meta":        ModMeta,
			},
			Roles: map[string]uint8{
				"host":   uint8(RoleHost),
				"target": uint8(RoleTarget),
				"both":   uint8(RoleBoth),
			},
			Capabilities: map[string]uint8{
				"intelligent_link": uint8(CapIntelligentLink),
				"direct_link":      uint8(CapDirectLink),
				"wifi_direct":      uint8(CapWiFiDirect),
				"dtls13":           uint8(CapDTLS13),
				"clipboard":        uint8(CapClipboard),
				"file_transfer":    uint8(CapFileTransfer),
			},
			MouseButtons: map[string]uint8{
				"left":    MouseButtonLeft,
				"right":   MouseButtonRight,
				"middle":  MouseButtonMiddle,
				"back":    MouseButtonBack,
				"forward": MouseButtonForward,
			},
		},
	}

	v.Cases = []VectorCase{
		// ── 跨平台修饰键换算（§6.3）──────────────────────────────
		newVectorCase("linux-ctrl-c-to-macos-cmd-c", PlatformLinux, PlatformMacOS,
			VectorEvent{Kind: "key_down", Key: HIDKeyC, Modifiers: ModLeftCtrl}),
		newVectorCase("macos-cmd-c-to-linux-ctrl-c", PlatformMacOS, PlatformLinux,
			VectorEvent{Kind: "key_down", Key: HIDKeyC, Modifiers: ModLeftMeta}),
		newVectorCase("linux-ctrl-key-to-macos-meta-key", PlatformLinux, PlatformMacOS,
			VectorEvent{Kind: "key_down", Key: HIDKeyLCtrl, Modifiers: ModLeftCtrl}),
		// KEY_UP 载荷不带掩码（§5.3），但键码仍要重映射：掩码没了，键位换算照样走。
		newVectorCase("linux-ctrl-keyup-to-macos-meta-keyup", PlatformLinux, PlatformMacOS,
			VectorEvent{Kind: "key_up", Key: HIDKeyLCtrl}),
		// 同平台：不动。
		newVectorCase("linux-ctrl-c-to-linux", PlatformLinux, PlatformLinux,
			VectorEvent{Kind: "key_down", Key: HIDKeyC, Modifiers: ModLeftCtrl}),
		// Ctrl 系之间（Windows↔Linux）不换。
		newVectorCase("windows-ctrl-c-to-linux", PlatformWindows, PlatformLinux,
			VectorEvent{Kind: "key_down", Key: HIDKeyC, Modifiers: ModLeftCtrl}),
		// Shift/Alt 直通，不参与 Ctrl⇄Meta 换算。
		newVectorCase("linux-shift-a-to-macos", PlatformLinux, PlatformMacOS,
			VectorEvent{Kind: "key_down", Key: HIDKeyA, Modifiers: ModLeftShift}),

		// ── 鼠标与滚轮：跨平台不换算，原样透传 ────────────────────
		newVectorCase("mouse-move", PlatformLinux, PlatformMacOS,
			VectorEvent{Kind: "mouse_move", DeltaX: 120, DeltaY: -45}),
		newVectorCase("mouse-button-left-press", PlatformLinux, PlatformMacOS,
			VectorEvent{Kind: "mouse_button", Button: MouseButtonLeft, Pressed: true}),
		newVectorCase("mouse-button-left-release", PlatformLinux, PlatformMacOS,
			VectorEvent{Kind: "mouse_button", Button: MouseButtonLeft, Pressed: false}),
		// 滚轮方向约定（§5.3）：水平正=向左，垂直正=向上。
		// evdev 的 REL_HWHEEL 正=向右，HAL 侧负责翻符号——向量里记的是**协议**约定。
		newVectorCase("scroll-horizontal-left", PlatformLinux, PlatformMacOS,
			VectorEvent{Kind: "scroll", DeltaY: 0, DeltaX: 3}),
		newVectorCase("scroll-vertical-up", PlatformLinux, PlatformMacOS,
			VectorEvent{Kind: "scroll", DeltaY: 1, DeltaX: 0}),
	}
	return v
}

// newVectorCase 造一个用例，并顺手用协议层算出每个事件的期望线上帧。
func newVectorCase(name string, from, to Platform, events ...VectorEvent) VectorCase {
	c := VectorCase{
		Name:   name,
		From:   platformKey(from),
		To:     platformKey(to),
		Events: events,
	}
	for _, ev := range events {
		c.Expect = append(c.Expect, expectFor(ev, from, to))
	}
	return c
}

// expectFor 用协议层自己的函数算出这一帧应该长成什么样：
// 编码走 EncodeInput（与各端流水线同一条路），字段再从载荷里解回来，
// 保证向量里的字段与字节永远一致。
func expectFor(ev VectorEvent, from, to Platform) VectorExpect {
	var in Input
	switch ev.Kind {
	case "key_down":
		in = Input{Kind: InputKeyDown, Key: ev.Key, Modifiers: ev.Modifiers}
	case "key_up":
		in = Input{Kind: InputKeyUp, Key: ev.Key}
	case "mouse_move":
		in = Input{Kind: InputMouseMove, DeltaX: ev.DeltaX, DeltaY: ev.DeltaY}
	case "mouse_button":
		in = Input{Kind: InputMouseButton, Button: ev.Button, Pressed: ev.Pressed}
	case "scroll":
		in = Input{Kind: InputScroll, DeltaX: ev.DeltaX, DeltaY: ev.DeltaY}
	default:
		panic("inputvectors: unknown event kind " + ev.Kind)
	}

	encoded, err := EncodeInput(in, from, to, nil, ContextGlobal)
	if err != nil {
		panic("inputvectors: encode failed: " + err.Error())
	}
	decoded, err := DecodeInput(encoded.FrameType, encoded.Payload)
	if err != nil {
		panic("inputvectors: decode failed: " + err.Error())
	}
	return VectorExpect{
		FrameType:  frameTypeName(encoded.FrameType),
		Key:        decoded.Key,
		Modifiers:  decoded.Modifiers,
		DeltaX:     decoded.DeltaX,
		DeltaY:     decoded.DeltaY,
		Button:     decoded.Button,
		Pressed:    decoded.Pressed,
		PayloadHex: hex.EncodeToString(encoded.Payload),
	}
}

// frameTypeName 是帧类型常量的稳定名字（向量文件里的 frame_type 字段）。
func frameTypeName(frameType uint8) string {
	switch frameType {
	case FrameKeyDown:
		return "key_down"
	case FrameKeyUp:
		return "key_up"
	case FrameMouseMove:
		return "mouse_move"
	case FrameMouseButton:
		return "mouse_button"
	case FrameScroll:
		return "scroll"
	default:
		return fmt.Sprintf("unknown_0x%02x", frameType)
	}
}
