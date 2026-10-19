// SendInput 注入（§13.2 Windows: SendInput()）。
//
// SAFETY：INPUT 的 union 在 Go 侧按「type + pad + 各自完整的成员结构」拆成
// 两个独立结构（keybdINPUT/mouseINPUT），字段偏移逐一对照 winuser.h 的
// KEYBDINPUT/MOUSEINPUT（64 位），只填充对应成员后整体传入 SendInput。
package wininput

import (
	"errors"
	"unsafe"

	"github.com/flowgrid/protocol"
	"golang.org/x/sys/windows"
)

// KEYBDINPUT（x64：2+2+4+4(+4pad)+8 = 24 字节）。
type keyboardInput struct {
	wVK         uint16
	wScan       uint16
	dwFlags     uint32
	time        uint32
	dwExtraInfo uintptr
}

// MOUSEINPUT（x64：4+4+4+4+4(+4pad)+8 = 32 字节）。
type mouseInput struct {
	dx          int32
	dy          int32
	mouseData   uint32
	dwFlags     uint32
	time        uint32
	dwExtraInfo uintptr
}

// INPUT = type(4) + pad(4) + union。
type keybdINPUT struct {
	inputType uint32
	_         uint32
	kbi       keyboardInput
}

type mouseINPUT struct {
	inputType uint32
	_         uint32
	mi        mouseInput
}

const (
	inputMouse    = 0
	inputKeyboard = 1

	keyeventfExtendedKey = 0x0001
	keyeventfKeyUp       = 0x0002
	keyeventfScanCode    = 0x0008

	mouseeventfMove       = 0x0001
	mouseeventfLeftDown   = 0x0002
	mouseeventfLeftUp     = 0x0004
	mouseeventfRightDown  = 0x0008
	mouseeventfRightUp    = 0x0010
	mouseeventfMiddleDown = 0x0020
	mouseeventfMiddleUp   = 0x0040
	mouseeventfXDown      = 0x0080
	mouseeventfXUp        = 0x0100
	mouseeventfWheel      = 0x0800
	mouseeventfHWheel     = 0x1000

	xbuttonX1 = 0x0001
	xbuttonX2 = 0x0002

	wheelDelta = 120
)

var errSendInput = errors.New("wininput: SendInput rejected the input")

func sendOne(p unsafe.Pointer, size uintptr) error {
	// SAFETY: p 指向按上面注释布局编排的 INPUT 结构，cbSize 显式传入。
	r, _, _ := procSendInput.Call(1, uintptr(p), size)
	if r == 0 {
		return errSendInput
	}
	return nil
}

func scanFromVK(vk uint16) uint16 {
	r, _, _ := procMapVirtualKeyW.Call(uintptr(vk), 0) // MAPVK_VK_TO_VSC
	return uint16(r)
}

// InjectKey 注入单个键事件（HID Usage → VK → 扫描码）。
func InjectKey(hid uint16, down bool) error {
	vk, ok := HIDToVK(hid)
	if !ok {
		return nil // §6.1 未覆盖的键位：静默忽略
	}
	inp := keybdINPUT{inputType: inputKeyboard}
	inp.kbi.wVK = vk
	inp.kbi.wScan = scanFromVK(vk)
	flags := uint32(keyeventfScanCode)
	if IsExtendedVK(vk) {
		flags |= keyeventfExtendedKey
	}
	if !down {
		flags |= keyeventfKeyUp
	}
	inp.kbi.dwFlags = flags
	return sendOne(unsafe.Pointer(&inp), unsafe.Sizeof(inp))
}

// InjectMouseMove 注入相对鼠标移动（§5.3 MOUSE_MOVE 像素语义）。
func InjectMouseMove(dx, dy int16) error {
	inp := mouseINPUT{inputType: inputMouse}
	inp.mi.dx = int32(dx)
	inp.mi.dy = int32(dy)
	inp.mi.dwFlags = mouseeventfMove
	return sendOne(unsafe.Pointer(&inp), unsafe.Sizeof(inp))
}

// InjectMouseButton 注入鼠标按键（§5.3 按钮号 1-5）。
func InjectMouseButton(button uint8, down bool) error {
	inp := mouseINPUT{inputType: inputMouse}
	switch button {
	case protocol.MouseButtonLeft:
		inp.mi.dwFlags = mouseeventfLeftDown
		if !down {
			inp.mi.dwFlags = mouseeventfLeftUp
		}
	case protocol.MouseButtonRight:
		inp.mi.dwFlags = mouseeventfRightDown
		if !down {
			inp.mi.dwFlags = mouseeventfRightUp
		}
	case protocol.MouseButtonMiddle:
		inp.mi.dwFlags = mouseeventfMiddleDown
		if !down {
			inp.mi.dwFlags = mouseeventfMiddleUp
		}
	case protocol.MouseButtonBack, protocol.MouseButtonForward:
		inp.mi.mouseData = xbuttonX1
		if button == protocol.MouseButtonForward {
			inp.mi.mouseData = xbuttonX2
		}
		inp.mi.dwFlags = mouseeventfXDown
		if !down {
			inp.mi.dwFlags = mouseeventfXUp
		}
	default:
		return nil
	}
	return sendOne(unsafe.Pointer(&inp), unsafe.Sizeof(inp))
}

// InjectScroll 注入滚轮（§5.3 像素语义 → WM_WHEEL 格：120 像素一格，保底 1 格）。
func InjectScroll(deltaY, deltaX int16) error {
	if deltaY != 0 {
		inp := mouseINPUT{inputType: inputMouse}
		inp.mi.dwFlags = mouseeventfWheel
		n := int32(deltaY) * wheelDelta
		if n == 0 {
			n = wheelDelta
		}
		inp.mi.mouseData = uint32(n)
		if err := sendOne(unsafe.Pointer(&inp), unsafe.Sizeof(inp)); err != nil {
			return err
		}
	}
	if deltaX != 0 {
		inp := mouseINPUT{inputType: inputMouse}
		inp.mi.dwFlags = mouseeventfHWheel
		n := int32(deltaX) * wheelDelta
		if n == 0 {
			n = wheelDelta
		}
		inp.mi.mouseData = uint32(n)
		if err := sendOne(unsafe.Pointer(&inp), unsafe.Sizeof(inp)); err != nil {
			return err
		}
	}
	return nil
}

// ForegroundApp 返回前台进程可执行文件名（§11.2 按应用映射上下文）。
func ForegroundApp() string {
	hwnd, _, _ := procGetForegroundWindow.Call()
	if hwnd == 0 {
		return ""
	}
	var pid uint32
	procGetWindowThreadProcessId.Call(hwnd, uintptr(unsafe.Pointer(&pid)))
	if pid == 0 {
		return ""
	}
	h, err := windows.OpenProcess(windows.PROCESS_QUERY_LIMITED_INFORMATION, false, pid)
	if err != nil {
		return ""
	}
	defer windows.CloseHandle(h)
	var buf [512]uint16
	size := uint32(len(buf))
	r1, _, _ := procQueryFullProcessImageNameW.Call(
		uintptr(h), 0, uintptr(unsafe.Pointer(&buf[0])), uintptr(unsafe.Pointer(&size)))
	if r1 == 0 {
		return ""
	}
	return windows.UTF16ToString(buf[:size])
}
