// SendInput 注入（§13.2 Windows: SendInput()）。
//
// SAFETY：INPUT 的 union 在 Go 侧按「type + pad + 各自完整的成员结构」拆成
// 两个独立结构（keybdINPUT/mouseINPUT），字段偏移逐一对照 winuser.h 的
// KEYBDINPUT/MOUSEINPUT（64 位），只填充对应成员后整体传入 SendInput。
package wininput

import (
	"errors"
	"sync"
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

// sendInput 是 SendInput 的间接层：单测替换它来观察/拦截注入，
// 避免在跑测试的桌面上真的按下按键。
var sendInput = sendOne

func scanFromVK(vk uint16) uint16 {
	r, _, _ := procMapVirtualKeyW.Call(uintptr(vk), 0) // MAPVK_VK_TO_VSC
	return uint16(r)
}

// InjectKey 注入单个键事件（HID Usage → VK → 扫描码）。
//
// modifiers 是发送端在 KEY_DOWN 里报告的修饰位。接收端必须据此补齐修饰键：
// keymap 把某键改写成 Ctrl+C 时只会改位掩码，不会额外发 Ctrl 的 KEY_DOWN；
// 只注入 C 的话目标机上根本不会出现组合键。
//
// 补齐的修饰键记为「合成」：协议 KEY_UP 不携带修饰位，若不在主键松开时
// 释放它们，Ctrl 会永久卡在目标机上（物理按住 Ctrl 的情形则由它自己的
// KEY_DOWN/KEY_UP 维护，不会误放）。
func InjectKey(hid uint16, down bool, modifiers uint8) error {
	vk, ok := HIDToVK(hid)
	if !ok {
		return nil // §6.1 未覆盖的键位：静默忽略
	}

	injectMu.Lock()
	defer injectMu.Unlock()

	// 修饰键自身的按下/松开直接反映到状态里，不再做「补齐」。
	if bit := protocol.ModifierFromSingleKey(hid); bit != 0 {
		if down {
			if injectMods&bit != 0 {
				// 已经按着（多半是之前由位掩码合成的）：收到真实按键只
				// 转移所有权，不再重复发一次 keyDown，避免目标机多一次重复。
				injectSynthetic &^= bit
				return nil
			}
			if err := injectKey(vk, hid, true); err != nil {
				return err
			}
			injectMods |= bit
			injectSynthetic &^= bit
			return nil
		}
		if injectMods&bit == 0 {
			return nil // 本来就没按着，别发多余的 keyUp
		}
		if err := injectKey(vk, hid, false); err != nil {
			return err
		}
		injectMods &^= bit
		return nil
	}

	if down {
		press, release := modifierChanges(injectMods, modifiers)
		for _, mhid := range release {
			if err := injectModifier(mhid, false); err != nil {
				return err
			}
		}
		for _, mhid := range press {
			if err := injectModifier(mhid, true); err != nil {
				return err
			}
			injectSynthetic |= protocol.ModifierFromSingleKey(mhid)
		}
		injectMods = modifiers & modifierMaskAll
		if err := injectKey(vk, hid, true); err != nil {
			return err
		}
		// 合成修饰位必须真实存在于目标机；位掩码里没有的位不该保留。
		injectSynthetic &= injectMods
		return nil
	}

	if err := injectKey(vk, hid, false); err != nil {
		return err
	}
	releaseSyntheticLocked()
	return nil
}

// 注入侧当前按下的修饰键（HID §6.2 位掩码）。多对端时可能并发注入，
// 而修饰键状态是整机共享的，必须串行化。injectSynthetic 记录由
// modifiers 位掩码补按（而非真实 KEY_DOWN）的那些位。
var (
	injectMu        sync.Mutex
	injectMods      uint8
	injectSynthetic uint8
	// injectDown 记录由本进程注入、当前仍处于按下状态的 HID。对端在按住键
	// 时断线不会补发 KEY_UP；不主动收尾的话，目标系统会一直认为键是按着的
	// （字符键还会持续自动重复）。
	injectDown    map[uint16]struct{}
	injectButtons uint8 // bit0..bit4 = 按钮 1..5
)

func releaseSyntheticLocked() {
	if injectSynthetic == 0 {
		return
	}
	for _, m := range modifierHIDs {
		if injectSynthetic&m.bit == 0 {
			continue
		}
		_ = injectModifier(m.hid, false)
	}
	injectSynthetic = 0
}

func injectModifier(hid uint16, down bool) error {
	vk, ok := HIDToVK(hid)
	if !ok {
		return nil
	}
	if err := injectKey(vk, hid, down); err != nil {
		return err
	}
	if bit := protocol.ModifierFromSingleKey(hid); bit != 0 {
		if down {
			injectMods |= bit
		} else {
			injectMods &^= bit
		}
	}
	return nil
}

func injectKey(vk, hid uint16, down bool) error {
	inp := keybdINPUT{inputType: inputKeyboard}
	inp.kbi.wVK = vk
	inp.kbi.wScan = scanFromVK(vk)
	flags := uint32(keyeventfScanCode)
	// 小键盘 Enter 与主 Enter 共用 VK_RETURN，只能靠 HID 区分扩展键。
	if IsExtendedVK(vk) || IsKeypadEnter(hid) {
		flags |= keyeventfExtendedKey
	}
	if !down {
		flags |= keyeventfKeyUp
	}
	inp.kbi.dwFlags = flags
	if err := sendInput(unsafe.Pointer(&inp), unsafe.Sizeof(inp)); err != nil {
		return err
	}
	if down {
		if injectDown == nil {
			injectDown = make(map[uint16]struct{})
		}
		injectDown[hid] = struct{}{}
	} else {
		delete(injectDown, hid)
	}
	return nil
}

// ReleaseAll 松开本进程注入且仍按着的所有键与鼠标按钮，并清空修饰键状态。
//
// 触发点：对端断线/切换托管/进程停止。SendInput 注入没有「设备断开自动
// 释放」这回事，必须显式补 KEY_UP/MOUSEUP；否则远程按住 Ctrl 时断线，
// 目标机会一直把 Ctrl 当作按下状态。
func ReleaseAll() {
	injectMu.Lock()
	defer injectMu.Unlock()
	releaseAllLocked()
}

func releaseAllLocked() {
	for button := uint8(1); button <= 5; button++ {
		if injectButtons&(1<<(button-1)) != 0 {
			_ = injectMouseButtonLocked(button, false)
		}
	}
	for hid := range injectDown {
		vk, ok := HIDToVK(hid)
		if !ok {
			continue
		}
		if bit := protocol.ModifierFromSingleKey(hid); bit != 0 {
			_ = injectModifier(hid, false)
		} else {
			_ = injectKey(vk, hid, false)
		}
	}
	// 兜底：极少数情况下 injectMods 里有位但 map 没记上（例如发送失败
	// 前状态已更新），也补一个 keyUp，宁可多发一次也不能让修饰键卡住。
	for _, m := range modifierHIDs {
		if injectMods&m.bit != 0 {
			_ = injectModifier(m.hid, false)
		}
	}
	injectDown = nil
	injectButtons = 0
	injectMods = 0
	injectSynthetic = 0
}

// InjectMouseMove 注入相对鼠标移动（§5.3 MOUSE_MOVE 像素语义）。
func InjectMouseMove(dx, dy int16) error {
	inp := mouseINPUT{inputType: inputMouse}
	inp.mi.dx = int32(dx)
	inp.mi.dy = int32(dy)
	inp.mi.dwFlags = mouseeventfMove
	return sendInput(unsafe.Pointer(&inp), unsafe.Sizeof(inp))
}

// InjectMouseButton 注入鼠标按键（§5.3 按钮号 1-5）。
func InjectMouseButton(button uint8, down bool) error {
	injectMu.Lock()
	defer injectMu.Unlock()
	return injectMouseButtonLocked(button, down)
}

func injectMouseButtonLocked(button uint8, down bool) error {
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
	if err := sendInput(unsafe.Pointer(&inp), unsafe.Sizeof(inp)); err != nil {
		return err
	}
	bit := uint8(1) << (button - 1)
	if down {
		injectButtons |= bit
	} else {
		injectButtons &^= bit
	}
	return nil
}

// InjectScroll 注入滚轮（协议滚轮单位 = 一格；WM_MOUSEWHEEL 的 120 = 一格）。
//
// 这里不能再乘 120：捕获侧已经把 Windows 的 ±120 折算成一格，注入侧乘回去
// 才是 1:1。历史上两边都乘过，物理滚一格会滚 120 格。
func InjectScroll(deltaY, deltaX int16) error {
	if deltaY != 0 {
		inp := mouseINPUT{inputType: inputMouse}
		inp.mi.dwFlags = mouseeventfWheel
		inp.mi.mouseData = uint32(int32(deltaY) * wheelDelta)
		if err := sendInput(unsafe.Pointer(&inp), unsafe.Sizeof(inp)); err != nil {
			return err
		}
	}
	if deltaX != 0 {
		inp := mouseINPUT{inputType: inputMouse}
		inp.mi.dwFlags = mouseeventfHWheel
		inp.mi.mouseData = horizontalWheelData(deltaX)
		if err := sendInput(unsafe.Pointer(&inp), unsafe.Sizeof(inp)); err != nil {
			return err
		}
	}
	return nil
}

// horizontalWheelData 把协议的水平滚动量（正=向左，§5.3）换成
// WM_MOUSEHWHEEL 的 mouseData（正=向右）。方向必须在这里翻一次，
// 否则 Windows↔Windows 的水平滚动会反向。
func horizontalWheelData(deltaX int16) uint32 {
	return uint32(-int32(deltaX) * wheelDelta)
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
