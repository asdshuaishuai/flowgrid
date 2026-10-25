//go:build linux

// OS 控制层的 C ABI 桥（Linux 实现）。
//
// 这一层把「协议层的 oscontrol 流水线」接给 C/C++ 客户端：
//
//	协议层（本包）                  平台层（oscontrol/evdev）
//	────────────────────────       ─────────────────────────
//	捕获 → 重映射 → 组帧            /dev/input/event* 读取
//	帧 → 解码 → 注入               /dev/uinput 写入
//
// 客户端拿到的是「帧」而不是「OS 事件」：它不写键位换算、不拼载荷、
// 不管修饰键合成——那些全在协议层与平台层里。
//
// 为什么是「拉取」而不是「回调」：控制端「停接管时把还按着的键松开」这件
// 事必须**回发 keyUp 帧**，也就是捕获侧本身要有发帧的出口。回调模型下这个
// 出口是 C 侧给的函数指针——而 **cgo 不支持 Go 去调一个 C 传来的函数指针**
// （typedef 出来的函数指针在 Go 侧是不透明类型，调不了）。拉取模型把这个
// 出口做成 Go 侧的队列：C 侧取帧，送上网线；松键时协议层把 keyUp 帧塞进
// 同一个队列，C 侧照样取得到。语义一样，少一层 FFI 魔法。
package main

/*
#include <stdint.h>
#include <stddef.h>
*/
import "C"

import (
	"context"
	"errors"
	"sync"
	"time"
	"unsafe"

	"github.com/flowgrid/protocol"
	"github.com/flowgrid/protocol/oscontrol"
	"github.com/flowgrid/protocol/oscontrol/evdev"
)

// init 把 Linux 的实现注册进 oscontrol 的注册表。
//
// 注册表是**依赖倒置**的：协议层不认识任何平台，各平台在启动时把自己
// 注册进来。没注册就是取不到（ErrNotSupported），而不是让协议层去 import
// 某个具体 OS。
func init() {
	oscontrol.Register(protocol.PlatformLinux, func() (oscontrol.Controller, error) {
		return evdev.New(), nil
	})
}

// queuedFrame 是待取走的一帧。
type queuedFrame struct {
	frameType uint8
	payload   []byte
}

// osControl 是 cgo 句柄背后的东西：一个控制器 + 一条捕获流水线 + 一条帧队列。
type osControl struct {
	controller oscontrol.Controller
	forwarder  *oscontrol.Forwarder
	injector   *oscontrol.Injector

	// frames 是「捕获侧的发帧出口」。捕获 goroutine 往里塞，C 侧取走。
	frames chan queuedFrame

	mu      sync.Mutex
	lastErr string

	cancel context.CancelFunc
	done   chan struct{} // 捕获 goroutine 退出时 close
	gone   chan struct{} // 等捕获 goroutine 真正结束
}

var osControls registry[*osControl]

// setError 记下最近一次失败的原因，供 C 侧取用。
func (h *osControl) setError(message string) {
	h.mu.Lock()
	h.lastErr = message
	h.mu.Unlock()
}

func (h *osControl) errorText() string {
	h.mu.Lock()
	defer h.mu.Unlock()
	return h.lastErr
}

// enqueue 把一帧放进出口队列。
//
// 队列满时不丢帧：发送方等。捕获侧慢下来是背压，不是丢事件——丢一个
// mouse_move 无所谓，丢一个 keyDown 就等于对端少按了一次键。
func (h *osControl) enqueue(frameType uint8, payload []byte) error {
	frame := queuedFrame{frameType: frameType, payload: payload}
	select {
	case h.frames <- frame:
		return nil
	case <-h.done:
		return errors.New("oscontrol: 捕获已停止")
	}
}

//export fg_oscontrol_new
//
// fg_oscontrol_new 造一个 OS 控制器。
//
// platform 取协议层的平台号（§3.1.2）。返回句柄（从 1 起；0 = 失败，
// 例如该平台没编进来）。
//
//export fg_oscontrol_new
func fg_oscontrol_new(platform C.uint8_t) C.uintptr_t {
	controller, err := oscontrol.For(protocol.Platform(platform))
	if err != nil {
		return 0
	}
	handle := &osControl{
		controller: controller,
		frames:     make(chan queuedFrame, 256),
		done:       make(chan struct{}),
		gone:       make(chan struct{}),
	}
	handle.forwarder = oscontrol.NewForwarder(controller, protocol.Platform(platform),
		func(frameType uint8, payload []byte) error {
			return handle.enqueue(frameType, payload)
		})
	handle.injector = oscontrol.NewInjector(controller)
	return C.uintptr_t(osControls.put(handle))
}

//export fg_oscontrol_free
//
// fg_oscontrol_free 释放句柄：停捕获、关设备。
//
//export fg_oscontrol_free
func fg_oscontrol_free(handle C.uintptr_t) {
	h, ok := osControls.get(uintptr(handle))
	if !ok {
		return
	}
	osControls.remove(uintptr(handle))
	fg_oscontrol_stop_capture(handle)
	_ = h.controller.Close()
}

//export fg_oscontrol_set_peer
//
// fg_oscontrol_set_peer 设对端平台与键位规则。
//
// 必须在 start_capture 之前设：重映射的方向取决于对端平台（§6.3
// Ctrl⇄Meta）。tableHandle 是 fg_keymap_load_file 的句柄，NULL = 无用户规则；
// context 传空 = 全局规则。
//
//export fg_oscontrol_set_peer
func fg_oscontrol_set_peer(handle C.uintptr_t, peerPlatform C.uint8_t,
	tableHandle unsafe.Pointer, context *C.char) C.int {
	h, ok := osControls.get(uintptr(handle))
	if !ok {
		return -1
	}
	var table *protocol.KeyMapTable
	if uintptr(tableHandle) != 0 {
		t, found := keymapTables.get(uintptr(tableHandle))
		if !found {
			h.setError("oscontrol: 键位规则句柄无效")
			return -1
		}
		table = t
	}
	ctx := protocol.KeyMapContext(goString(context))
	if ctx == "" {
		ctx = protocol.ContextGlobal
	}
	h.forwarder = oscontrol.NewForwarder(h.controller, protocol.Platform(peerPlatform),
		func(frameType uint8, payload []byte) error {
			return h.enqueue(frameType, payload)
		},
		oscontrol.WithKeyMap(table),
		oscontrol.WithContext(ctx))
	return 0
}

//export fg_oscontrol_start_capture
//
// fg_oscontrol_start_capture 开始捕获。
//
// 之后协议层把「捕获 → 重映射 → 组帧」整条走完，产出的帧进入出口队列，
// 由 C 侧用 fg_oscontrol_next_frame 取走。
//
// 返回 0 成功；非 0 表示打不开设备（原因见 fg_oscontrol_last_error）。
//
//export fg_oscontrol_start_capture
func fg_oscontrol_start_capture(handle C.uintptr_t) C.int {
	h, ok := osControls.get(uintptr(handle))
	if !ok {
		return -1
	}
	if err := h.controller.StartCapture(); err != nil {
		h.setError(err.Error())
		return -1
	}
	ctx, cancel := context.WithCancel(context.Background())
	h.mu.Lock()
	h.cancel = cancel
	h.mu.Unlock()

	go func() {
		defer close(h.gone)
		if err := h.forwarder.Run(ctx); err != nil && !errors.Is(err, context.Canceled) {
			h.setError(err.Error())
		}
	}()
	return 0
}

//export fg_oscontrol_stop_capture
//
// fg_oscontrol_stop_capture 停止捕获。
//
// **返回后捕获 goroutine 已经退出**，不会再有新帧进队列；但队列里已有的帧
// 仍然取得到（松键的 keyUp 帧就是靠这条通路送出去的）。
//
//export fg_oscontrol_stop_capture
func fg_oscontrol_stop_capture(handle C.uintptr_t) {
	h, ok := osControls.get(uintptr(handle))
	if !ok {
		return
	}
	h.mu.Lock()
	cancel := h.cancel
	h.cancel = nil
	h.mu.Unlock()

	// 先放行可能正卡在「队列满」上的捕获 goroutine，再让它退出。
	close(h.done)
	if cancel != nil {
		cancel()
	}
	<-h.gone
}

//export fg_oscontrol_next_frame
//
// fg_oscontrol_next_frame 取一帧待发的输入帧。
//
// 返回 1 = 取到；0 = 超时（没有新帧）；-1 = 出错。
// 取到的载荷由 C 侧用 fg_free_buffer 释放。
//
// timeoutMs < 0 表示一直等（直到有帧或捕获停止）。
//
//export fg_oscontrol_next_frame
func fg_oscontrol_next_frame(handle C.uintptr_t, timeoutMs C.int,
	outType *C.uint8_t, outPayload **C.uint8_t, outLen *C.size_t) C.int {
	h, ok := osControls.get(uintptr(handle))
	if !ok {
		return -1
	}
	var timeout <-chan time.Time
	if timeoutMs >= 0 {
		timer := time.NewTimer(time.Duration(timeoutMs) * time.Millisecond)
		defer timer.Stop()
		timeout = timer.C
	}
	select {
	case frame := <-h.frames:
		*outType = C.uint8_t(frame.frameType)
		*outLen = C.size_t(len(frame.payload))
		*outPayload = cBuffer(frame.payload)
		return 1
	case <-timeout:
		*outType = 0
		*outPayload = nil
		*outLen = 0
		return 0
	}
}

//export fg_oscontrol_release_captured
//
// fg_oscontrol_release_captured 松开控制端还按着的键与鼠标按钮。
//
// 它把 keyUp / mouseButton(up) 帧塞进出口队列——对端在另一台机器上，我们
// 不再发松开帧的话那边的键会一直卡在按下态。停接管、对端断开时都要调。
//
//export fg_oscontrol_release_captured
func fg_oscontrol_release_captured(handle C.uintptr_t) C.int {
	h, ok := osControls.get(uintptr(handle))
	if !ok {
		return -1
	}
	if err := h.forwarder.ReleasePressed(); err != nil {
		h.setError(err.Error())
		return -1
	}
	return 0
}

//export fg_oscontrol_start_injection
//
// fg_oscontrol_start_injection 开始注入（打开 /dev/uinput）。
//
// 返回 0 成功；非 0 表示没有 uinput 权限（原因见 fg_oscontrol_last_error）。
//
//export fg_oscontrol_start_injection
func fg_oscontrol_start_injection(handle C.uintptr_t) C.int {
	h, ok := osControls.get(uintptr(handle))
	if !ok {
		return -1
	}
	if err := h.controller.StartInjection(); err != nil {
		h.setError(err.Error())
		return -1
	}
	return 0
}

//export fg_oscontrol_stop_injection
//
// fg_oscontrol_stop_injection 停注入并松开本机还按着的键。
//
//export fg_oscontrol_stop_injection
func fg_oscontrol_stop_injection(handle C.uintptr_t) {
	h, ok := osControls.get(uintptr(handle))
	if !ok {
		return
	}
	_ = h.controller.ReleasePressed()
}

//export fg_oscontrol_inject_frame
//
// fg_oscontrol_inject_frame 喂一帧入站输入帧：协议层解码 → 注入本机。
//
// 收端**不二次重映射**（当前各端都是「发前重映射」）。
//
//export fg_oscontrol_inject_frame
func fg_oscontrol_inject_frame(handle C.uintptr_t, frameType C.uint8_t,
	payload *C.uint8_t, length C.size_t) C.int {
	h, ok := osControls.get(uintptr(handle))
	if !ok {
		return -1
	}
	var data []byte
	if payload != nil && length > 0 {
		// GoBytes 会把 C 内存拷进 Go 内存：注入是即时的，用完即弃。
		data = C.GoBytes(unsafe.Pointer(payload), C.int(length))
	}
	if err := h.injector.InjectFrame(uint8(frameType), data); err != nil {
		h.setError(err.Error())
		return -1
	}
	return 0
}

//export fg_oscontrol_release_injected
//
// fg_oscontrol_release_injected 松开本机还按着的键与鼠标按钮。
//
//export fg_oscontrol_release_injected
func fg_oscontrol_release_injected(handle C.uintptr_t) C.int {
	h, ok := osControls.get(uintptr(handle))
	if !ok {
		return -1
	}
	if err := h.injector.ReleasePressed(); err != nil {
		h.setError(err.Error())
		return -1
	}
	return 0
}

//export fg_oscontrol_last_error
//
// fg_oscontrol_last_error 取最近一次失败的说明（UTF-8，NUL 结尾）。
//
// bufLen 是 buf 的字节数。返回写入的字节数（不含 NUL）；buf 为空或 bufLen
// 为 0 时只返回长度，不写任何东西。
//
//export fg_oscontrol_last_error
func fg_oscontrol_last_error(handle C.uintptr_t, buf *C.char, bufLen C.size_t) C.int {
	h, ok := osControls.get(uintptr(handle))
	if !ok {
		return 0
	}
	message := h.errorText()
	if buf == nil || bufLen == 0 {
		return C.int(len(message))
	}
	size := int(bufLen)
	raw := []byte(message)
	if len(raw) > size-1 {
		raw = raw[:size-1]
	}
	copy(unsafe.Slice((*byte)(unsafe.Pointer(buf)), size), raw)
	*(*byte)(unsafe.Pointer(uintptr(unsafe.Pointer(buf)) + uintptr(len(raw)))) = 0
	return C.int(len(message))
}
