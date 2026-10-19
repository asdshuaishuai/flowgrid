// user32/kernel32 过程声明（仅本包使用）。
package wininput

import "golang.org/x/sys/windows"

var (
	moduser32   = windows.NewLazySystemDLL("user32.dll")
	modkernel32 = windows.NewLazySystemDLL("kernel32.dll")

	procSendInput                  = moduser32.NewProc("SendInput")
	procMapVirtualKeyW             = moduser32.NewProc("MapVirtualKeyW")
	procGetForegroundWindow        = moduser32.NewProc("GetForegroundWindow")
	procGetWindowThreadProcessId   = moduser32.NewProc("GetWindowThreadProcessId")
	procSetWindowsHookExW          = moduser32.NewProc("SetWindowsHookExW")
	procUnhookWindowsHookEx        = moduser32.NewProc("UnhookWindowsHookEx")
	procCallNextHookEx             = moduser32.NewProc("CallNextHookEx")
	procGetMessageW                = moduser32.NewProc("GetMessageW")
	procPostThreadMessageW         = moduser32.NewProc("PostThreadMessageW")
	procGetMessageExtraInfo        = moduser32.NewProc("GetMessageExtraInfo")
	procRegisterClassExW           = moduser32.NewProc("RegisterClassExW")
	procCreateWindowExW            = moduser32.NewProc("CreateWindowExW")
	procDefWindowProcW             = moduser32.NewProc("DefWindowProcW")
	procAddClipboardFormatListener = moduser32.NewProc("AddClipboardFormatListener")
	procOpenClipboard              = moduser32.NewProc("OpenClipboard")
	procCloseClipboard             = moduser32.NewProc("CloseClipboard")
	procEmptyClipboard             = moduser32.NewProc("EmptyClipboard")
	procSetClipboardData           = moduser32.NewProc("SetClipboardData")
	procGetClipboardData           = moduser32.NewProc("GetClipboardData")
	procGlobalAlloc                = modkernel32.NewProc("GlobalAlloc")
	procGlobalFree                 = modkernel32.NewProc("GlobalFree")
	procGetModuleHandleW           = modkernel32.NewProc("GetModuleHandleW")
	procGlobalLock                 = modkernel32.NewProc("GlobalLock")
	procGlobalUnlock               = modkernel32.NewProc("GlobalUnlock")
	procQueryFullProcessImageNameW = modkernel32.NewProc("QueryFullProcessImageNameW")
)
