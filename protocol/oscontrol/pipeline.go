package oscontrol

import (
	"context"
	"fmt"

	"github.com/flowgrid/protocol"
)

// Forwarder 把「本机捕获的输入」变成「对端要的线上帧」。
//
// 这一段过去在每个客户端各写一遍（deepin 的 C++ Forwarder、mac 的
// InputCaptureTap.handle），于是「哪种操作用哪种帧、载荷怎么拼、键位怎么
// 重映射」被各端各写一份，迟早分叉。现在它归协议层：客户端只要提供一个
// Controller（怎么抓、怎么注入）与一个 sink（把帧送出去）。
type Forwarder struct {
	controller Controller
	peer       protocol.Platform
	table      *protocol.KeyMapTable
	context    protocol.KeyMapContext
	// sink 收到「帧类型 + 载荷」，由调用方送上网线。
	sink func(frameType uint8, payload []byte) error
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
	f := &Forwarder{controller: controller, peer: peer, sink: sink}
	for _, opt := range opts {
		opt(f)
	}
	return f
}

// Run 循环：捕获 → 协议层重映射并编码 → 交给 sink。
//
// ctx 取消就停；Capture 返回错误就返回（设备拔了、权限没了——不该吞掉，
// 吞掉的话界面会一直显示「接管中」而其实早就不动了）。
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
		frameType, payload, err := protocol.EncodeInput(in, from, f.peer, f.table, f.context)
		if err != nil {
			// 这一条编不出来（不该发生：事件模型是协议定的），跳过继续，
			// 但别把整条流水线带下去。
			continue
		}
		if err := f.sink(frameType, payload); err != nil {
			return err
		}
	}
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
