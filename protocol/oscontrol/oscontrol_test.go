package oscontrol_test

import (
	"context"
	"errors"
	"testing"

	"github.com/flowgrid/protocol"
	"github.com/flowgrid/protocol/oscontrol"
)

// fakeController 是假的 OS 控制实现：按脚本吐出捕获事件，记录注入动作。
//
// 用它测流水线，是因为流水线要验的是「捕获 → 编码 → 帧」与「帧 → 解码 → 注入」
// 这两段协议逻辑，跟具体 OS 无关。真机那一段由 oscontrol/evdev 的测试负责。
type fakeController struct {
	platform protocol.Platform
	captured []protocol.Input
	injected []protocol.Input
	closed   bool
	started  bool
}

func (f *fakeController) Platform() protocol.Platform { return f.platform }

func (f *fakeController) StartCapture() error {
	f.started = true
	return nil
}

func (f *fakeController) StartInjection() error {
	f.started = true
	return nil
}

func (f *fakeController) Capture() (protocol.Input, error) {
	if len(f.captured) == 0 {
		return protocol.Input{}, errors.New("没有更多事件了")
	}
	in := f.captured[0]
	f.captured = f.captured[1:]
	return in, nil
}

func (f *fakeController) Inject(in protocol.Input) error {
	f.injected = append(f.injected, in)
	return nil
}

func (f *fakeController) ReleasePressed() error { return nil }
func (f *fakeController) Close() error           { f.closed = true; return nil }

type sentFrame struct {
	frameType uint8
	payload   []byte
}

func TestForwarderEncodesThroughProtocolLayer(t *testing.T) {
	// Linux 客户端把 Ctrl+C 发给 macOS：协议层应重映射成 Cmd+C。
	controller := &fakeController{
		platform: protocol.PlatformLinux,
		captured: []protocol.Input{
			{Kind: protocol.InputKeyDown, Key: protocol.HIDKeyC, Modifiers: protocol.ModLeftCtrl},
			{Kind: protocol.InputKeyUp, Key: protocol.HIDKeyC},
		},
	}
	var sent []sentFrame
	forwarder := oscontrol.NewForwarder(controller, protocol.PlatformMacOS,
		func(frameType uint8, payload []byte) error {
			sent = append(sent, sentFrame{frameType, payload})
			return nil
		})

	// 脚本跑完 Capture 会返回错误 → Run 结束；那是预期的终止，不是失败。
	if err := forwarder.Run(context.Background()); err == nil {
		t.Fatal("事件用完时 Run 应该返回 Capture 的错误")
	}
	if len(sent) != 2 {
		t.Fatalf("产出 %d 帧，期望 2 帧", len(sent))
	}
	if sent[0].frameType != protocol.FrameKeyDown {
		t.Errorf("第 0 帧类型 0x%02x，期望 KEY_DOWN 0x%02x", sent[0].frameType, protocol.FrameKeyDown)
	}
	// 键位 0x06 不变，掩码 0x01(Ctrl) → 0x08(Meta)。
	want := []byte{0x00, 0x06, protocol.ModLeftMeta}
	if string(sent[0].payload) != string(want) {
		t.Errorf("第 0 帧载荷 %x，期望 %x", sent[0].payload, want)
	}
	if sent[1].frameType != protocol.FrameKeyUp {
		t.Errorf("第 1 帧类型 0x%02x，期望 KEY_UP 0x%02x", sent[1].frameType, protocol.FrameKeyUp)
	}
}

func TestForwarderKeyUpStillRemapsKeyCode(t *testing.T) {
	// 松开 Ctrl：载荷不带掩码，但键位仍要换算（对端是 macOS 时必须发 Meta 的
	// keyUp，否则对端的 Cmd 一直按着）。
	controller := &fakeController{
		platform: protocol.PlatformLinux,
		captured: []protocol.Input{
			{Kind: protocol.InputKeyUp, Key: protocol.HIDKeyLCtrl},
		},
	}
	var sent []sentFrame
	forwarder := oscontrol.NewForwarder(controller, protocol.PlatformMacOS,
		func(frameType uint8, payload []byte) error {
			sent = append(sent, sentFrame{frameType, payload})
			return nil
		})
	_ = forwarder.Run(context.Background())

	if len(sent) != 1 {
		t.Fatalf("产出 %d 帧，期望 1 帧", len(sent))
	}
	want := []byte{0x00, 0xE3} // HIDKeyLCtrl → HIDKeyLMeta
	if string(sent[0].payload) != string(want) {
		t.Errorf("keyUp 载荷 %x，期望 %x", sent[0].payload, want)
	}
}

func TestForwarderReleasesPressedKeysWithRemappedCodes(t *testing.T) {
	// 控制端按下 Ctrl+C（对端是 macOS），然后没等 keyUp 就停接管。
	// 转发器必须把还按着的键松开，而且 keyUp 里的键位要跟 keyDown **一样**
	// （重映射之后的）——对端在另一台机器上，键位对不上那边就一直按着。
	controller := &fakeController{
		platform: protocol.PlatformLinux,
		captured: []protocol.Input{
			{Kind: protocol.InputKeyDown, Key: protocol.HIDKeyC, Modifiers: protocol.ModLeftCtrl},
		},
	}
	var sent []sentFrame
	forwarder := oscontrol.NewForwarder(controller, protocol.PlatformMacOS,
		func(frameType uint8, payload []byte) error {
			sent = append(sent, sentFrame{frameType, payload})
			return nil
		})
	_ = forwarder.Run(context.Background())

	if len(sent) != 1 {
		t.Fatalf("跑完应有 1 帧（keyDown），实际 %d", len(sent))
	}
	if err := forwarder.ReleasePressed(); err != nil {
		t.Fatalf("松开失败: %v", err)
	}
	if len(sent) != 2 {
		t.Fatalf("松开后应有 2 帧，实际 %d", len(sent))
	}
	released := sent[1]
	if released.frameType != protocol.FrameKeyUp {
		t.Fatalf("第 1 帧类型 0x%02x，期望 KEY_UP 0x%02x", released.frameType, protocol.FrameKeyUp)
	}
	// keyDown 用的键位是 0x06（C 不变）；keyUp 必须是同一个键位。
	want := []byte{0x00, 0x06}
	if string(released.payload) != string(want) {
		t.Errorf("松开的载荷 %x，期望 %x", released.payload, want)
	}

	// 再调一次不该重复发：已经松开了。
	if err := forwarder.ReleasePressed(); err != nil {
		t.Fatalf("第二次松开失败: %v", err)
	}
	if len(sent) != 2 {
		t.Errorf("第二次松开又多发了 %d 帧", len(sent)-2)
	}
}

func TestForwarderReleasesPressedModifierWithRemappedCode(t *testing.T) {
	// 按下左 Ctrl（对端 macOS → 要发 Meta）。停接管时 keyUp 必须带 Meta 的
	// 键位，否则对端的 Cmd 一直按着。
	controller := &fakeController{
		platform: protocol.PlatformLinux,
		captured: []protocol.Input{
			{Kind: protocol.InputKeyDown, Key: protocol.HIDKeyLCtrl, Modifiers: protocol.ModLeftCtrl},
		},
	}
	var sent []sentFrame
	forwarder := oscontrol.NewForwarder(controller, protocol.PlatformMacOS,
		func(frameType uint8, payload []byte) error {
			sent = append(sent, sentFrame{frameType, payload})
			return nil
		})
	_ = forwarder.Run(context.Background())
	if err := forwarder.ReleasePressed(); err != nil {
		t.Fatalf("松开失败: %v", err)
	}
	if len(sent) != 2 {
		t.Fatalf("应有 2 帧，实际 %d", len(sent))
	}
	// HIDKeyLCtrl(0xE0) → HIDKeyLMeta(0xE3)。
	want := []byte{0x00, 0xE3}
	if string(sent[1].payload) != string(want) {
		t.Errorf("松开的载荷 %x，期望 %x（重映射后的 Meta 键位）", sent[1].payload, want)
	}
}

func TestForwarderReleasesPressedMouseButton(t *testing.T) {
	controller := &fakeController{
		platform: protocol.PlatformLinux,
		captured: []protocol.Input{
			{Kind: protocol.InputMouseButton, Button: protocol.MouseButtonLeft, Pressed: true},
		},
	}
	var sent []sentFrame
	forwarder := oscontrol.NewForwarder(controller, protocol.PlatformLinux,
		func(frameType uint8, payload []byte) error {
			sent = append(sent, sentFrame{frameType, payload})
			return nil
		})
	_ = forwarder.Run(context.Background())
	if err := forwarder.ReleasePressed(); err != nil {
		t.Fatalf("松开失败: %v", err)
	}
	if len(sent) != 2 {
		t.Fatalf("应有 2 帧，实际 %d", len(sent))
	}
	if sent[1].frameType != protocol.FrameMouseButton {
		t.Fatalf("第 1 帧类型 0x%02x，期望 MOUSE_BUTTON 0x%02x", sent[1].frameType, protocol.FrameMouseButton)
	}
	want := (&protocol.MouseButtonPayload{ButtonID: protocol.MouseButtonLeft, State: 0}).Marshal()
	if string(sent[1].payload) != string(want) {
		t.Errorf("松开的载荷 %x，期望 %x", sent[1].payload, want)
	}
}

func TestInjectorDecodesAndInjects(t *testing.T) {
	controller := &fakeController{platform: protocol.PlatformLinux}
	injector := oscontrol.NewInjector(controller)

	payload := (&protocol.MouseMovePayload{DeltaX: 120, DeltaY: -45}).Marshal()
	if err := injector.InjectFrame(protocol.FrameMouseMove, payload); err != nil {
		t.Fatalf("注入失败: %v", err)
	}
	if len(controller.injected) != 1 {
		t.Fatalf("注入了 %d 次，期望 1 次", len(controller.injected))
	}
	got := controller.injected[0]
	if got.Kind != protocol.InputMouseMove || got.DeltaX != 120 || got.DeltaY != -45 {
		t.Errorf("注入的是 %+v，期望 mouse_move(120,-45)", got)
	}
}

func TestInjectorRejectsNonInputFrame(t *testing.T) {
	controller := &fakeController{platform: protocol.PlatformLinux}
	injector := oscontrol.NewInjector(controller)
	if err := injector.InjectFrame(protocol.FrameClipboard, []byte{0x00}); err == nil {
		t.Error("非输入帧应该报错")
	}
	if len(controller.injected) != 0 {
		t.Errorf("非输入帧不该被注入，却注入了 %d 次", len(controller.injected))
	}
}

func TestRegistry(t *testing.T) {
	platform := protocol.PlatformAndroid
	oscontrol.Unregister(platform)
	if _, err := oscontrol.For(platform); !errors.Is(err, oscontrol.ErrNotSupported) {
		t.Fatalf("没注册的平台应该报 ErrNotSupported，实际 %v", err)
	}

	controller := &fakeController{platform: platform}
	oscontrol.Register(platform, func() (oscontrol.Controller, error) { return controller, nil })
	defer oscontrol.Unregister(platform)

	got, err := oscontrol.For(platform)
	if err != nil {
		t.Fatalf("取实现失败: %v", err)
	}
	if got.Platform() != platform {
		t.Errorf("实现报的平台 0x%02x，期望 0x%02x", uint8(got.Platform()), uint8(platform))
	}
}
