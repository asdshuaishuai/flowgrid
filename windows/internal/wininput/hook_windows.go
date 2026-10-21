// 低级钩子捕获（WH_KEYBOARD_LL / WH_MOUSE_LL）。
//
// 钩子回调运行在装了钩子的泵线程上，必须在回调内同步做出「吞 or 透传」
// 决定（hosting 判定走无锁读取），事件本体入队后由消费方转发——回调里做
// 重活会被系统移除钩子。
package wininput

import (
	"errors"
	"runtime"
	"sync"
	"sync/atomic"
	"unsafe"

	"github.com/flowgrid/protocol"
	"golang.org/x/sys/windows"
)

const (
	whKeyboardLL = 13
	whMouseLL    = 14

	wmMouseMove   = 0x0200
	wmLButtonDown = 0x0201
	wmLButtonUp   = 0x0202
	wmRButtonDown = 0x0204
	wmRButtonUp   = 0x0205
	wmMButtonDown = 0x0207
	wmMButtonUp   = 0x0208
	wmMouseWheel  = 0x020A
	wmXButtonDown = 0x020B
	wmXButtonUp   = 0x020C
	wmMouseHWheel = 0x020E
	wmKeydown     = 0x0100
	wmKeyup       = 0x0101
	wmSyskeydown  = 0x0104
	wmSyskeyup    = 0x0105
	wmQuit        = 0x0012

	llkhfExtended = 0x01
	llkhfInjected = 0x10
	llkhfUp       = 0x80

	// msLLHOOKSTRUCT.flags
	llmhfInjected = 0x01
)

// InputEvent 是一条捕获事件（HID 空间）。
type InputEvent struct {
	Kind      int // 0=Key 1=MouseMove 2=MouseButton 3=Wheel
	HID       uint16
	Down      bool
	Modifiers uint8
	DX, DY    int16
	Button    uint8
	DeltaY    int16
}

const (
	KindKey = iota
	KindMouseMove
	KindMouseButton
	KindWheel
)

// kbdLLHOOKSTRUCT（x64）。
type kbdLLHookStruct struct {
	vkCode      uint32
	scanCode    uint32
	flags       uint32
	time        uint32
	dwExtraInfo uintptr
}

// msLLHOOKSTRUCT（x64）。
type mouseLLHookStruct struct {
	ptX         int32
	ptY         int32
	mouseData   uint32
	flags       uint32
	time        uint32
	dwExtraInfo uintptr
}

type Hooks struct {
	events  chan InputEvent
	hosting func() bool
	thread  atomic.Uint32
	stopped atomic.Bool

	modMask uint32 // 泵线程独占：当前修饰键掩码（HID §6.2）
	lastX   int32
	lastY   int32
	hasLast bool

	// 高精度滚轮不足一格的余数（泵线程独占）。
	wheelRemY int32
	wheelRemX int32

	wg sync.WaitGroup
}

// StartHooks 安装低级钩子并起泵线程。hosting 在钩子回调内同步调用，
// 必须无锁且快速（返回 true = 吞掉本机输入并转发）。
func StartHooks(hosting func() bool) (*Hooks, error) {
	if hosting == nil {
		return nil, errors.New("wininput: hosting predicate is nil")
	}
	h := &Hooks{
		events:  make(chan InputEvent, 512),
		hosting: hosting,
	}
	ready := make(chan error, 1)
	h.wg.Add(1)
	go func() {
		runtime.LockOSThread()
		defer runtime.UnlockOSThread()
		defer h.wg.Done()
		h.pump(ready)
	}()
	if err := <-ready; err != nil {
		return nil, err
	}
	return h, nil
}

func (h *Hooks) Events() <-chan InputEvent { return h.events }

func (h *Hooks) Stop() {
	if h.stopped.Swap(true) {
		return
	}
	tid := h.thread.Load()
	if tid != 0 {
		procPostThreadMessageW.Call(uintptr(tid), wmQuit, 0, 0)
	}
	h.wg.Wait()
}

func (h *Hooks) pump(ready chan<- error) {
	h.thread.Store(windows.GetCurrentThreadId())
	hookCb := windows.NewCallback(h.hookProc)
	kb, _, err := procSetWindowsHookExW.Call(whKeyboardLL, hookCb, 0, 0)
	if kb == 0 {
		ready <- errors.New("wininput: SetWindowsHookEx(keyboard) failed: " + err.Error())
		return
	}
	ms, _, err := procSetWindowsHookExW.Call(whMouseLL, hookCb, 0, 0)
	if ms == 0 {
		procUnhookWindowsHookEx.Call(kb)
		ready <- errors.New("wininput: SetWindowsHookEx(mouse) failed: " + err.Error())
		return
	}
	ready <- nil // 钩子装好了；此后进入消息泵直到 WM_QUIT

	var msg [7]uintptr // MSG：hwnd, message, wParam, lParam, time, pt.x, pt.y
	for {
		// GetMessageW 返回 -1=错误 0=WM_QUIT 其余>0
		r, _, _ := procGetMessageW.Call(uintptr(unsafe.Pointer(&msg[0])), 0, 0, 0)
		if r == 0 || int64(r) == ^int64(0) { // WM_QUIT 或错误
			return
		}
	}
}

// hookProc 钩子回调：决定吞/透传，事件入队。nCode<0 必须直透。
// 键盘事件无论托管与否都入队（切换热键 Ctrl+Alt+数字在非托管态也要能被
// hub 看到）；吞掉与否只由 hosting 决定——回调内同步判定，不拖慢钩子。
func (h *Hooks) hookProc(nCode int32, wparam, lparam uintptr) uintptr {
	if nCode < 0 {
		r, _, _ := procCallNextHookEx.Call(0, uintptr(nCode), wparam, lparam)
		return r
	}
	hosting := h.hosting()
	isKey := wparam == wmKeydown || wparam == wmKeyup || wparam == wmSyskeydown || wparam == wmSyskeyup

	swallow := false
	if isKey {
		st := (*kbdLLHookStruct)(unsafe.Pointer(lparam))
		// SendInput 注入的事件会再次进钩子。双向/同机场景下若照常入队，
		// A 注入的键会被 A 再捕获并转发给 B，B 注入后又发回来，形成回环。
		// 注入事件既不吞也不处理（不污染本机修饰键状态）。
		if st.flags&llkhfInjected != 0 {
			r, _, _ := procCallNextHookEx.Call(0, uintptr(nCode), wparam, lparam)
			return r
		}
		vk := uint16(st.vkCode)
		down := wparam == wmKeydown || wparam == wmSyskeydown
		if hid, ok := VKToHID(vk); ok {
			// 小键盘 Enter 与主 Enter 同为 VK_RETURN，用扩展键标志区分。
			if vk == vkReturn && st.flags&llkhfExtended != 0 {
				hid = protocol.HIDKeyKpEnter
			}
			if m, isMod := ModifiersFromVK(vk); isMod {
				if down {
					h.modMask |= uint32(m)
				} else {
					h.modMask &^= uint32(m)
				}
			}
			ev := InputEvent{Kind: KindKey, HID: hid, Down: down, Modifiers: uint8(h.modMask)}
			select {
			case h.events <- ev:
			default: // 队列满宁可丢事件也不卡钩子
			}
		}
		swallow = hosting
	} else {
		st := (*mouseLLHookStruct)(unsafe.Pointer(lparam))
		if st.flags&llmhfInjected != 0 {
			r, _, _ := procCallNextHookEx.Call(0, uintptr(nCode), wparam, lparam)
			return r
		}
		switch wparam {
		case wmMouseMove:
			if hosting {
				if h.hasLast {
					ev := InputEvent{Kind: KindMouseMove,
						DX: int16(clampDelta(st.ptX - h.lastX)),
						DY: int16(clampDelta(st.ptY - h.lastY))}
					select {
					case h.events <- ev:
					default:
					}
				}
				h.lastX, h.lastY, h.hasLast = st.ptX, st.ptY, true
			} else {
				h.hasLast = false
			}
			swallow = hosting
		case wmLButtonDown, wmLButtonUp, wmRButtonDown, wmRButtonUp,
			wmMButtonDown, wmMButtonUp, wmXButtonDown, wmXButtonUp:
			if hosting {
				down := wparam == wmLButtonDown || wparam == wmRButtonDown ||
					wparam == wmMButtonDown || wparam == wmXButtonDown
				ev := InputEvent{Kind: KindMouseButton, Down: down,
					Button: buttonIDForMsg(uint32(wparam), uint16(st.mouseData>>16))}
				select {
				case h.events <- ev:
				default:
				}
			}
			swallow = hosting
		case wmMouseWheel, wmMouseHWheel:
			if hosting {
				// Windows 的滚轮 delta 是 120 的倍数；协议单位是「格」，
				// 必须在这里折算，注入侧再乘回 120。
				delta := int32(int16(st.mouseData >> 16))
				ev := InputEvent{Kind: KindWheel}
				if wparam == wmMouseWheel {
					ev.DeltaY = wheelLines(delta, &h.wheelRemY)
				} else {
					ev.DX = wheelLines(-delta, &h.wheelRemX)
				}
				if ev.DeltaY != 0 || ev.DX != 0 {
					select {
					case h.events <- ev:
					default:
					}
				}
			}
			swallow = hosting
		}
	}
	if swallow {
		return 1
	}
	r, _, _ := procCallNextHookEx.Call(0, uintptr(nCode), wparam, lparam)
	return r
}

func buttonIDForMsg(msg uint32, xbutton uint16) uint8 {
	switch msg {
	case wmLButtonDown, wmLButtonUp:
		return protocol.MouseButtonLeft
	case wmRButtonDown, wmRButtonUp:
		return protocol.MouseButtonRight
	case wmMButtonDown, wmMButtonUp:
		return protocol.MouseButtonMiddle
	case wmXButtonDown, wmXButtonUp:
		if xbutton == xbuttonX2 {
			return protocol.MouseButtonForward
		}
		return protocol.MouseButtonBack
	}
	return 0
}

func clampDelta(d int32) int16 {
	if d > 32767 {
		return 32767
	}
	if d < -32768 {
		return -32768
	}
	return int16(d)
}
