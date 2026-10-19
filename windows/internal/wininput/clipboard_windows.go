// 剪贴板平台面：消息窗监听 WM_CLIPBOARDUPDATE + CF_UNICODETEXT 读写。
package wininput

import (
	"errors"
	"runtime"
	"sync"
	"sync/atomic"
	"time"
	"unsafe"

	"golang.org/x/sys/windows"
)

const (
	cfUnicodeText     = 13
	wmClipboardUpdate = 0x031D
	gmemMoveable      = 0x0002
	hwndMessage       = ^uintptr(2) // HWND_MESSAGE = (HWND)-3
)

type wndClassEx struct {
	cbSize       uint32
	style        uint32
	wndProc      uintptr
	clsExtra     int32
	wndExtra     int32
	instance     windows.Handle
	icon         windows.Handle
	cursor       windows.Handle
	brBackground windows.Handle
	menuName     uintptr
	className    uintptr
	iconSm       windows.Handle
}

// MSG（x64）：hwnd(8) message(4) pad(4) wParam(8) lParam(8) time(4) pt(8)。
type msgStruct struct {
	hwnd    windows.HWND
	message uint32
	_       uint32
	wParam  uintptr
	lParam  uintptr
	time    uint32
	ptX     int32
	ptY     int32
}

var (
	clipMu         sync.Mutex
	activeOnChange atomic.Pointer[func()]
	clipWndProc    = windows.NewCallback(clipWndProcImpl)
)

type clipboardPumpState struct {
	threadID uint32
	err      error
}

// StartClipboardListener 建消息窗并监听剪贴板变更。onChange 在泵线程触发，
// 必须快速返回（重活自行转 goroutine）。stop 幂等。
func StartClipboardListener(onChange func()) (stop func(), err error) {
	ready := make(chan clipboardPumpState, 1)
	go func() {
		runtime.LockOSThread()
		defer runtime.UnlockOSThread()
		clipboardPump(onChange, ready)
	}()
	st := <-ready
	if st.err != nil {
		return nil, st.err
	}
	var once sync.Once
	return func() {
		once.Do(func() {
			procPostThreadMessageW.Call(uintptr(st.threadID), wmQuit, 0, 0)
		})
	}, nil
}

func clipboardPump(onChange func(), ready chan clipboardPumpState) {
	className, _ := windows.UTF16FromString("FlowGridClipboardSink")
	instance, _, _ := procGetModuleHandleW.Call(0)

	var cls wndClassEx
	cls.cbSize = uint32(unsafe.Sizeof(cls))
	cls.wndProc = clipWndProc
	cls.instance = windows.Handle(instance)
	cls.className = uintptr(unsafe.Pointer(&className[0]))
	if r, _, _ := procRegisterClassExW.Call(uintptr(unsafe.Pointer(&cls))); r == 0 {
		ready <- clipboardPumpState{err: errors.New("wininput: RegisterClassEx failed")}
		return
	}

	hwnd, _, callErr := procCreateWindowExW.Call(0,
		uintptr(unsafe.Pointer(&className[0])), uintptr(unsafe.Pointer(&className[0])),
		0, 0, 0, 0, 0, hwndMessage, 0, instance, 0)
	if hwnd == 0 {
		ready <- clipboardPumpState{err: errors.New("wininput: CreateWindowEx failed: " + callErr.Error())}
		return
	}

	activeOnChange.Store(&onChange)
	if r, _, callErr := procAddClipboardFormatListener.Call(hwnd); r == 0 {
		activeOnChange.Store(nil)
		ready <- clipboardPumpState{err: errors.New("wininput: AddClipboardFormatListener failed: " + callErr.Error())}
		return
	}

	ready <- clipboardPumpState{threadID: windows.GetCurrentThreadId()}

	var msg msgStruct
	for {
		r, _, _ := procGetMessageW.Call(uintptr(unsafe.Pointer(&msg)), 0, 0, 0)
		if r == 0 || int64(r) == ^int64(0) { // WM_QUIT / 错误
			return
		}
		procDefWindowProcW.Call(uintptr(msg.hwnd), uintptr(msg.message), msg.wParam, msg.lParam)
	}
}

// clipWndProcImpl 消息窗回调：只关心 WM_CLIPBOARDUPDATE，其余走默认处理。
func clipWndProcImpl(hwnd uintptr, msg uint32, wp, lp uintptr) uintptr {
	if msg == wmClipboardUpdate {
		if cb := activeOnChange.Load(); cb != nil && *cb != nil {
			(*cb)()
		}
	}
	r, _, _ := procDefWindowProcW.Call(hwnd, uintptr(msg), wp, lp)
	return r
}

func openClipboardWithRetry() bool {
	for i := 0; i < 10; i++ {
		if r, _, _ := procOpenClipboard.Call(0); r != 0 {
			return true
		}
		time.Sleep(20 * time.Millisecond)
	}
	return false
}

func globalAlloc(flags uint32, size uintptr) (uintptr, uintptr, error) {
	return procGlobalAlloc.Call(uintptr(flags), size)
}

func globalFree(h uintptr) error {
	procGlobalFree.Call(h)
	return nil
}

// ReadClipboardText 读 CF_UNICODETEXT（无文本或剪贴板被占用返回 false）。
func ReadClipboardText() (string, bool) {
	clipMu.Lock()
	defer clipMu.Unlock()
	if !openClipboardWithRetry() {
		return "", false
	}
	defer procCloseClipboard.Call()
	h, _, _ := procGetClipboardData.Call(cfUnicodeText)
	if h == 0 {
		return "", false
	}
	p, _, _ := procGlobalLock.Call(h)
	if p == 0 {
		return "", false
	}
	defer procGlobalUnlock.Call(h)
	// SAFETY: GlobalLock 区间内 [p, p+2MB) 可读；用 Slice 视图避免指针运算。
	buf := unsafe.Slice((*uint16)(unsafe.Pointer(p)), 1<<20)
	u16 := buf[:0]
	for _, c := range buf {
		if c == 0 {
			break
		}
		u16 = append(u16, c)
	}
	if len(u16) == 0 {
		return "", false
	}
	return windows.UTF16ToString(u16), true
}

// WriteClipboardText 写 CF_UNICODETEXT（覆盖既有内容）。
func WriteClipboardText(text string) error {
	clipMu.Lock()
	defer clipMu.Unlock()
	if !openClipboardWithRetry() {
		return errors.New("wininput: OpenClipboard failed")
	}
	defer procCloseClipboard.Call()

	u16, err := windows.UTF16FromString(text)
	if err != nil {
		return err
	}
	size := uintptr(len(u16)) * 2
	procEmptyClipboard.Call()
	h, _, callErr := globalAlloc(gmemMoveable, size)
	if h == 0 {
		return errors.New("wininput: GlobalAlloc failed: " + callErr.Error())
	}
	p, _, _ := procGlobalLock.Call(h)
	if p == 0 {
		_ = globalFree(h)
		return errors.New("wininput: GlobalLock failed")
	}
	copy(unsafe.Slice((*byte)(unsafe.Pointer(p)), size), unsafe.Slice((*byte)(unsafe.Pointer(&u16[0])), size))
	procGlobalUnlock.Call(h)
	if r, _, _ := procSetClipboardData.Call(cfUnicodeText, h); r == 0 {
		_ = globalFree(h) // 失败时所有权未转移，需自行释放
		return errors.New("wininput: SetClipboardData failed")
	}
	return nil
}
