//go:build darwin

// realinput：协议驱动「真实」键鼠注入的最小 HAL 演示（放慢放大 + 读回诊断版）。
//
// protocol 包把键鼠操作编成 HID 帧 → DecodeInputFrame 识别成 InputEvent → 经 cgo
// CoreGraphics 注入本机：鼠标 CGWarpMouseCursorPosition（无需辅助功能权限），键盘
// CGEvent（需辅助功能权限）。能力（编码/识别）全在协议层，这里只做 OS 注入。
package main

import (
	"fmt"
	"os"
	"strconv"
	"time"

	"github.com/flowgrid/protocol"
)

/*
#cgo LDFLAGS: -framework CoreGraphics -framework ApplicationServices
#include <CoreGraphics/CoreGraphics.h>
#include <ApplicationServices/ApplicationServices.h>

int fgAxTrusted(void) { return AXIsProcessTrusted() ? 1 : 0; }

void fgScreen(double *w, double *h) {
	CGRect r = CGDisplayBounds(CGMainDisplayID());
	*w = r.size.width; *h = r.size.height;
}

void fgCursor(double *x, double *y) {
	CGEventRef e = CGEventCreate(NULL);
	CGPoint p = CGEventGetLocation(e);
	*x = p.x; *y = p.y;
	CFRelease(e);
}

// fgWarp 绝对移动光标（CGWarp 无需辅助功能权限）；返回 CGError。
int fgWarp(double x, double y) { return (int)CGWarpMouseCursorPosition(CGPointMake(x, y)); }

// fgKey 注入键事件；mods 用协议 §6.2 位掩码。
int fgKey(unsigned short vk, int down, unsigned char mods) {
	CGEventRef e = CGEventCreateKeyboardEvent(NULL, (CGKeyCode)vk, down ? true : false);
	if (!e) return -1;
	CGEventFlags f = 0;
	if (mods & 0x01) f |= kCGEventFlagMaskControl;
	if (mods & 0x02) f |= kCGEventFlagMaskShift;
	if (mods & 0x04) f |= kCGEventFlagMaskAlternate;
	if (mods & 0x08) f |= kCGEventFlagMaskCommand;
	CGEventSetFlags(e, f);
	CGEventPost(kCGHIDEventTap, e);
	CFRelease(e);
	return 0;
}
*/
import "C"

// hidToMacVK：演示用到的键 HID Usage ID → macOS 虚拟键码（客户端 HAL 的键码翻译）。
var hidToMacVK = map[uint16]uint16{
	protocol.HIDKeySpace:  0x31, // Space
	protocol.HIDKeyEscape: 0x35, // Esc
	protocol.HIDKeyLMeta:  0x37, // 左 Command
}

var held uint8 // 当前按住的修饰位（协议 §6.2），keyup 时带上

func main() {
	if len(os.Args) == 4 && os.Args[1] == "warp" {
		x, _ := strconv.ParseFloat(os.Args[2], 64)
		y, _ := strconv.ParseFloat(os.Args[3], 64)
		C.fgWarp(C.double(x), C.double(y))
		fmt.Printf("warped cursor to (%.0f, %.0f)\n", x, y)
		return
	}
	ax := C.fgAxTrusted() == 1
	var sw, sh C.double
	C.fgScreen(&sw, &sh)
	W, H := float64(sw), float64(sh)
	fmt.Printf("主屏 %.0fx%.0f · 辅助功能权限 %v\n", W, H, ax)

	sx, sy := readCursor()
	fmt.Printf("当前光标 (%.0f, %.0f)\n", sx, sy)

	cx, cy := W/2, H/2
	fmt.Print("\n倒计时，请盯着光标：3… ")
	time.Sleep(time.Second)
	fmt.Print("2… ")
	time.Sleep(time.Second)
	fmt.Print("1… ")
	time.Sleep(time.Second)
	fmt.Println(" GO")

	// ① 缓慢移到屏幕中心，停 1.5s，再读回确认光标是否真的被移动。
	fmt.Printf("→ 经协议把光标缓慢移到中心 (%.0f, %.0f)…\n", cx, cy)
	moveCursorSlow(sx, sy, cx, cy, 1200*time.Millisecond)
	time.Sleep(1500 * time.Millisecond)
	ex, ey := readCursor()
	if abs(ex-cx) < 6 && abs(ey-cy) < 6 {
		fmt.Printf("   读回光标 (%.0f, %.0f) → 已到中心 ✓ 协议驱动的移动真生效了\n", ex, ey)
	} else {
		fmt.Printf("   读回光标 (%.0f, %.0f) → 没到中心 ✗ warp 未生效（会话/权限问题）\n", ex, ey)
	}

	// ② 绕中心画一个大方形（±250px），慢速 + 每角停 0.5s。
	fmt.Printf("→ 绕中心画 ±250px 大方形（很慢，注意看）…\n")
	half := 250.0
	path := [][2]float64{
		{cx - half, cy - half}, {cx + half, cy - half},
		{cx + half, cy + half}, {cx - half, cy + half},
		{cx - half, cy - half},
	}
	px, py := ex, ey
	for _, p := range path {
		moveCursorSlow(px, py, p[0], p[1], 900*time.Millisecond)
		px, py = p[0], p[1]
		time.Sleep(500 * time.Millisecond)
	}
	fmt.Println("✓ 鼠标演示完成")

	// ③ 键盘：Cmd+Space 打开聚焦停 3s，再 Esc 关闭。
	if !ax {
		fmt.Println("⚠ 未授权辅助功能，键盘注入会被系统忽略。")
		return
	}
	fmt.Printf("\n→ 键盘：Cmd+Space 打开聚焦（停 3 秒）…\n")
	press(protocol.HIDKeySpace, protocol.ModLeftMeta)
	time.Sleep(40 * time.Millisecond)
	release(protocol.HIDKeySpace)
	time.Sleep(40 * time.Millisecond)
	release(protocol.HIDKeyLMeta)
	time.Sleep(3 * time.Second)
	fmt.Printf("→ Esc 关闭聚焦…\n")
	press(protocol.HIDKeyEscape, 0)
	time.Sleep(40 * time.Millisecond)
	release(protocol.HIDKeyEscape)
	fmt.Println("✓ 键盘演示完成")
}

// moveCursorSlow 从 (x0,y0) 缓慢移到 (x1,y1)：每步经协议 FrameMouseMove → DecodeInputFrame → CGWarp。
func moveCursorSlow(x0, y0, x1, y1 float64, dur time.Duration) {
	steps := int(dur / (40 * time.Millisecond))
	if steps < 1 {
		steps = 1
	}
	dx := (x1 - x0) / float64(steps)
	dy := (y1 - y0) / float64(steps)
	x, y := x0, y0
	for i := 0; i < steps; i++ {
		payload := (&protocol.MouseMovePayload{DeltaX: clamp16(dx), DeltaY: clamp16(dy)}).Marshal()
		ev, err := protocol.DecodeInputFrame(protocol.FrameMouseMove, payload, protocol.PlatformMacOS, protocol.PlatformMacOS, nil, "")
		if err != nil {
			continue
		}
		x += float64(ev.DeltaX)
		y += float64(ev.DeltaY)
		if rc := C.fgWarp(C.double(x), C.double(y)); rc != 0 {
			fmt.Printf("   [warp rc=%d @ step %d]\n", int(rc), i)
		}
		time.Sleep(40 * time.Millisecond)
	}
	C.fgWarp(C.double(x1), C.double(y1)) // 精确落点
}

// press：经协议构造 KEY_DOWN → DecodeInputFrame → CGEvent 注入，并记录修饰位。
func press(hid uint16, mods uint8) {
	payload := (&protocol.KeyDownPayload{KeyCode: hid, Modifiers: mods}).Marshal()
	ev, err := protocol.DecodeInputFrame(protocol.FrameKeyDown, payload, protocol.PlatformMacOS, protocol.PlatformMacOS, nil, "")
	if err != nil {
		fmt.Println("  键解码失败:", err)
		return
	}
	C.fgKey(C.ushort(macVK(ev.KeyCode)), 1, C.uchar(ev.Modifiers))
	held |= ev.Modifiers
}

// release：注入 KEY_UP，带上当前按住 Modifiers（松开修饰键则清对应位）。
func release(hid uint16) {
	flags := held
	if bit := protocol.ModifierFromSingleKey(hid); bit != 0 {
		flags &^= bit
	}
	C.fgKey(C.ushort(macVK(hid)), 0, C.uchar(flags))
	held = flags
}

func readCursor() (float64, float64) {
	var x, y C.double
	C.fgCursor(&x, &y)
	return float64(x), float64(y)
}

func macVK(hid uint16) uint16 {
	if vk, ok := hidToMacVK[hid]; ok {
		return vk
	}
	return 0
}

func clamp16(f float64) int16 {
	if f > 32767 {
		return 32767
	}
	if f < -32768 {
		return -32768
	}
	return int16(f)
}

func abs(f float64) float64 {
	if f < 0 {
		return -f
	}
	return f
}
