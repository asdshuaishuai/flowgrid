package protocol

// 规范化的输入事件模型 + 「操作 ↔ 线上字节」的对称编解码。
//
// 为什么有这一层：各平台原本各自定义「捕获到的事件」——deepin 是
// `EventKind + pressed 位 + deltaX/deltaY`，mac 是 CGEvent 的原生字段
// （virtualKey / flagsRaw）。于是「修饰键怎么表达」「水平滚轮哪边算正」
// 「按下的键和松开的键怎么区分」这些**协议语义**被各端各写一遍，迟早分叉；
// 而且分叉了也没人知道是哪端错了。
//
// 统一到这一份模型之后，协议层才谈得上「按 os 类型调用 os 层，不关心实现」：
// 各端只做「OS 原生事件 ↔ Input」的翻译，帧怎么拼、键位怎么换算，都在协议层。

import "fmt"

// InputKind 是一次用户操作的种类。
//
// 值与帧类型**一一对应且相同**（key_down ↔ KEY_DOWN 0x01 …）：这样客户端
// 直接把帧类型常量当操作类型传，不必再镜像一套「操作类型」常量——多镜像
// 一套就多一个会漂移的地方。
type InputKind uint8

const (
	InputKeyDown     InputKind = InputKind(FrameKeyDown)
	InputKeyUp       InputKind = InputKind(FrameKeyUp)
	InputMouseMove   InputKind = InputKind(FrameMouseMove)
	InputMouseButton InputKind = InputKind(FrameMouseButton)
	InputScroll      InputKind = InputKind(FrameScroll)
)

// String 给日志用。
func (k InputKind) String() string {
	switch k {
	case InputKeyDown:
		return "key_down"
	case InputKeyUp:
		return "key_up"
	case InputMouseMove:
		return "mouse_move"
	case InputMouseButton:
		return "mouse_button"
	case InputScroll:
		return "scroll"
	default:
		return fmt.Sprintf("unknown_0x%02x", uint8(k))
	}
}

// Input 是一次用户操作的规范表示（OS 无关）。
//
// 字段的单位一律按协议来：Key 是 HID Usage ID（§6.1），Modifiers 是 §6.2 的
// 位掩码，DeltaX/DeltaY 是协议方向的水平/垂直位移（水平正=向左，垂直正=向上，
// §5.3），Button 是 §6.4 的按钮号。
type Input struct {
	Kind      InputKind
	Key       uint16 // HID Usage ID (§6.1)
	Modifiers uint8  // §6.2 位掩码
	DeltaX    int16
	DeltaY    int16
	Button    uint8 // §6.4
	Pressed   bool
}

// EncodedInput 是一次操作编成线上帧的结果。
//
// Key/Modifiers 是**重映射之后**的键位与掩码（只有键事件有值）。
//
// 为什么要把它们带出来：停接管时要把还按着的键松开，而 keyUp 必须跟
// keyDown 用**同一个**键码——重映射的输入掩码不同，算出来的键码可能对不上，
// 对端就会多出一个按下的键（那台机器上的键一直卡着）。编码器已经算过一次
// 重映射，让它顺手把结果交出来，比让调用方再算一遍可靠——两遍算法一旦
// 分叉，「按下」和「松开」就对不上了，而且极难查。
type EncodedInput struct {
	FrameType uint8
	Payload   []byte
	Key       uint16
	Modifiers uint8
}

// EncodeInput 把一次操作编成线上帧（帧类型 + 载荷）。
//
// 这是 DecodeInputFrame 的对称函数：协议层因此拥有「操作 ↔ 线上字节」的完整
// 语义，各端不必再各写一份「哪种操作用哪种帧、载荷怎么拼」。
//
// 重映射在这里做（**发前重映射**，§6.3）：键事件按 from→to 换算键位与掩码；
// 鼠标与滚轮不参与跨平台换算，原样透传。
func EncodeInput(in Input, from, to Platform, table *KeyMapTable, context KeyMapContext) (EncodedInput, error) {
	if context == "" {
		context = ContextGlobal
	}
	switch in.Kind {
	case InputKeyDown:
		key, mods := RemapCapturedKey(in.Key, in.Modifiers, from, to, table, context)
		return EncodedInput{
			FrameType: FrameKeyDown,
			Payload:   (&KeyDownPayload{KeyCode: key, Modifiers: mods}).Marshal(),
			Key:       key,
			Modifiers: mods,
		}, nil
	case InputKeyUp:
		// KEY_UP 载荷不带掩码（§5.3），但**键码仍然要重映射**：掩码没了，
		// 键位换算照样得按目标平台走（对端是 macOS 时，松 Ctrl 必须发 Meta 的
		// keyUp，否则对端的 Cmd 会一直按着）。
		key, _ := RemapCapturedKey(in.Key, 0, from, to, table, context)
		return EncodedInput{
			FrameType: FrameKeyUp,
			Payload:   (&KeyUpPayload{KeyCode: key}).Marshal(),
			Key:       key,
		}, nil
	case InputMouseMove:
		return EncodedInput{
			FrameType: FrameMouseMove,
			Payload:   (&MouseMovePayload{DeltaX: in.DeltaX, DeltaY: in.DeltaY}).Marshal(),
		}, nil
	case InputMouseButton:
		state := uint8(0)
		if in.Pressed {
			state = 1
		}
		return EncodedInput{
			FrameType: FrameMouseButton,
			Payload:   (&MouseButtonPayload{ButtonID: in.Button, State: state}).Marshal(),
		}, nil
	case InputScroll:
		return EncodedInput{
			FrameType: FrameScroll,
			Payload:   (&ScrollPayload{DeltaY: in.DeltaY, DeltaX: in.DeltaX}).Marshal(),
		}, nil
	default:
		return EncodedInput{}, NewError(ErrInvalidPayload, fmt.Sprintf("unknown input kind %d", uint8(in.Kind)))
	}
}

// DecodeInput 把线上帧解回一次操作。
//
// **不做重映射**：当前三端都是「发前重映射」（发端按目标平台换算好再发），
// 收端再换算一次就把键位换错了。要改成「发原始键、收端重映射」时，收端走
// DecodeInputFrame(from=对端, to=本机) 那条路（见 docs/INPUT-CAPABILITY.md 后续项）。
func DecodeInput(frameType uint8, payload []byte) (Input, error) {
	switch frameType {
	case FrameKeyDown:
		p, err := UnmarshalKeyDown(payload)
		if err != nil {
			return Input{}, err
		}
		return Input{Kind: InputKeyDown, Key: p.KeyCode, Modifiers: p.Modifiers}, nil
	case FrameKeyUp:
		p, err := UnmarshalKeyUp(payload)
		if err != nil {
			return Input{}, err
		}
		return Input{Kind: InputKeyUp, Key: p.KeyCode}, nil
	case FrameMouseMove:
		p, err := UnmarshalMouseMove(payload)
		if err != nil {
			return Input{}, err
		}
		return Input{Kind: InputMouseMove, DeltaX: p.DeltaX, DeltaY: p.DeltaY}, nil
	case FrameMouseButton:
		p, err := UnmarshalMouseButton(payload)
		if err != nil {
			return Input{}, err
		}
		return Input{Kind: InputMouseButton, Button: p.ButtonID, Pressed: p.State == 0x01}, nil
	case FrameScroll:
		p, err := UnmarshalScroll(payload)
		if err != nil {
			return Input{}, err
		}
		return Input{Kind: InputScroll, DeltaX: p.DeltaX, DeltaY: p.DeltaY}, nil
	default:
		return Input{}, NewError(ErrInvalidPayload, fmt.Sprintf("not an input frame (0x%02x)", frameType))
	}
}
