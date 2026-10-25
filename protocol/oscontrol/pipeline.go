package oscontrol

import (
	"context"
	"fmt"
	"sync"

	"github.com/flowgrid/protocol"
)

// Forwarder 把「本机捕获的输入」变成「对端要的线上帧」。
//
// 这一段过去在每个客户端各写一遍（deepin 的 C++ Forwarder、mac 的
// InputCaptureTap.handle），于是「哪种操作用哪种帧、载荷怎么拼、键位怎么
// 重映射」被各端各写一份，迟早分叉。现在它归协议层：客户端只要提供一个
// Controller（怎么抓、怎么注入）与一个 sink（把帧送出去）。
//
// 转发器还负责一件容易被漏掉的事：**记住哪些键还按着**（按的是重映射之后
// 的键位）。停接管时要靠它把还按着的键松开——对端在另一台机器上，我们不
// 再发 keyUp 的话那边的键会一直卡在按下态。
type Forwarder struct {
	controller Controller
	peer       protocol.Platform
	table      *protocol.KeyMapTable
	context    protocol.KeyMapContext
	// sink 收到「帧类型 + 载荷」，由调用方送上网线。
	sink func(frameType uint8, payload []byte) error

	mu sync.Mutex
	// pressed 记的是**源键位 → 重映射后键位**。
	//
	// 为什么记源键位而不是直接记重映射后的：keyUp 的重映射输入掩码是 0
	// （载荷不带掩码），如果规则表按掩码匹配，同一个物理键在 keyDown 与
	// keyUp 上可能算出不同的键位——用重映射后的键位做键，就对不上。用源键
	// 位做键、按下的那次记下重映射结果，就稳了。
	pressed map[uint16]uint16
	// pressedButtons 记的是鼠标按钮号（§6.4，不参与重映射）。
	pressedButtons map[uint8]struct{}
}

// ForwarderOption 是可选参数。
type ForwarderOption func(*Forwarder)

// WithKeyMap 指定键位规则表（§11）。不指定就是只用平台默认换算。
func WithKeyMap(table *protocol.KeyMapTable) ForwarderOption {
	return func(f *Forwarder) { f.table = table }
}

// WithContext 指定规则上下文（空 = 全局规则）。
func WithContext(context protocol.KeyMapContext) ForwarderOption {
	return func(f *Forwarder) { f.context = context }
}

// NewForwarder 造一个转发器。
func NewForwarder(controller Controller, peer protocol.Platform,
	sink func(frameType uint8, payload []byte) error, opts ...ForwarderOption) *Forwarder {
	f := &Forwarder{
		controller:     controller,
		peer:           peer,
		sink:           sink,
		pressed:        map[uint16]uint16{},
		pressedButtons: map[uint8]struct{}{},
	}
	for _, opt := range opts {
		opt(f)
	}
	return f
}

// Run 循环：捕获 → 协议层重映射并编码 → 交给 sink。
//
// ctx 取消就停；Capture 返回错误就返回（设备拔了、权限没了——不该吞掉，
// 吞掉的话界面会一直显示「接管中」而其实早就不动了)。
func (f *Forwarder) Run(ctx context.Context) error {
	if f.controller == nil {
		return fmt.Errorf("oscontrol: Forwarder 没有 Controller")
	}
	if f.sink == nil {
		return fmt.Errorf("oscontrol: Forwarder 没有 sink")
	}
	from := f.controller.Platform()
	for {
		select {
		case <-ctx.Done():
			return ctx.Err()
		default:
		}

		in, err := f.controller.Capture()
		if err != nil {
			return err
		}
		encoded, err := protocol.EncodeInput(in, from, f.peer, f.table, f.context)
		if err != nil {
			// 这一条编不出来（不该发生：事件模型是协议定的），跳过继续，
			// 但别把整条流水线带下去。
			continue
		}
		f.track(in, encoded)
		if err := f.sink(encoded.FrameType, encoded.Payload); err != nil {
			return err
		}
	}
}

// track 记住「还按着哪些键 / 哪些鼠标按钮」。
//
// 记的是重映射**之后**的键位：keyUp 必须跟 keyDown 用同一个键码发出去，
// 否则对端会多出一个按下的键。
func (f *Forwarder) track(in protocol.Input, encoded protocol.EncodedInput) {
	f.mu.Lock()
	defer f.mu.Unlock()
	switch in.Kind {
	case protocol.InputKeyDown:
		f.pressed[in.Key] = encoded.Key
	case protocol.InputKeyUp:
		delete(f.pressed, in.Key)
	case protocol.InputMouseButton:
		if in.Pressed {
			f.pressedButtons[in.Button] = struct{}{}
		} else {
			delete(f.pressedButtons, in.Button)
		}
	}
}

// ReleasePressed 把还按着的键与鼠标按钮松开：往 sink 发 keyUp /
// mouseButton(up) 帧。
//
// 什么时候必须做：停接管、对端断开、会话切换。对端在**另一台机器**上，
// 我们不再发 keyUp 的话那边的键会一直停在按下态——典型症状是「远程机器上
// 有个键卡住了」。
//
// 松开用的键位是**按下时记下的那个**（已经重映射过），不重新算：重映射的
// 输入掩码不同，算出来的键码可能对不上。
func (f *Forwarder) ReleasePressed() error {
	if f.sink == nil {
		return fmt.Errorf("oscontrol: Forwarder 没有 sink")
	}
	f.mu.Lock()
	keys := make([]uint16, 0, len(f.pressed))
	for _, remapped := range f.pressed {
		keys = append(keys, remapped)
	}
	buttons := make([]uint8, 0, len(f.pressedButtons))
	for button := range f.pressedButtons {
		buttons = append(buttons, button)
	}
	f.pressed = map[uint16]uint16{}
	f.pressedButtons = map[uint8]struct{}{}
	f.mu.Unlock()

	for _, key := range keys {
		payload := (&protocol.KeyUpPayload{KeyCode: key}).Marshal()
		if err := f.sink(protocol.FrameKeyUp, payload); err != nil {
			return err
		}
	}
	for _, button := range buttons {
		payload := (&protocol.MouseButtonPayload{ButtonID: button, State: 0}).Marshal()
		if err := f.sink(protocol.FrameMouseButton, payload); err != nil {
			return err
		}
	}
	return nil
}

// Injector 把对端发来的线上帧注入本机。
//
// 收端**不二次重映射**：当前三端都是「发前重映射」，收端再换算一次就把
// 键位换错了。要改成「发原始键、收端重映射」时，这里改调
// protocol.DecodeInputFrame(from=对端, to=本机)。
type Injector struct {
	controller Controller
}

// NewInjector 造一个注入器。
func NewInjector(controller Controller) *Injector {
	return &Injector{controller: controller}
}

// InjectFrame 解一帧并注入。
func (i *Injector) InjectFrame(frameType uint8, payload []byte) error {
	if i.controller == nil {
		return fmt.Errorf("oscontrol: Injector 没有 Controller")
	}
	in, err := protocol.DecodeInput(frameType, payload)
	if err != nil {
		return err
	}
	return i.controller.Inject(in)
}

// ReleasePressed 松开还按着的键与按钮。
func (i *Injector) ReleasePressed() error {
	if i.controller == nil {
		return fmt.Errorf("oscontrol: Injector 没有 Controller")
	}
	return i.controller.ReleasePressed()
}
