//go:build windows

package wininput

import (
	"testing"
	"unsafe"

	"github.com/flowgrid/protocol"
)

// ReleaseAll 必须在断线/切换/停止时把注入过且仍按着的键与鼠标键松开，
// 否则远程按住 Ctrl 时掉线，目标机会永久认为 Ctrl 还按着。
func TestReleaseAllReleasesInjectedKeysAndButtons(t *testing.T) {
	injectMu.Lock()
	injectDown = nil
	injectButtons = 0
	injectMods = 0
	injectSynthetic = 0
	injectMu.Unlock()

	orig := sendInput
	var keyFlags, mouseFlags []uint32
	sendInput = func(p unsafe.Pointer, size uintptr) error {
		// SAFETY: 第一个字段是 INPUT.type，调用方传的都是本文件的
		// keybdINPUT/mouseINPUT，按 type 分派到对应的完整结构。
		switch *(*uint32)(p) {
		case inputKeyboard:
			keyFlags = append(keyFlags, (*keybdINPUT)(p).kbi.dwFlags)
		case inputMouse:
			mouseFlags = append(mouseFlags, (*mouseINPUT)(p).mi.dwFlags)
		}
		return nil
	}
	t.Cleanup(func() {
		sendInput = orig
		injectMu.Lock()
		injectDown = nil
		injectButtons = 0
		injectMods = 0
		injectSynthetic = 0
		injectMu.Unlock()
	})

	if err := InjectKey(protocol.HIDKeyLCtrl, true, 0); err != nil {
		t.Fatalf("ctrl down: %v", err)
	}
	if err := InjectKey(protocol.HIDKeyA, true, protocol.ModLeftCtrl); err != nil {
		t.Fatalf("A down: %v", err)
	}
	if err := InjectMouseButton(protocol.MouseButtonLeft, true); err != nil {
		t.Fatalf("left down: %v", err)
	}

	ReleaseAll()

	injectMu.Lock()
	defer injectMu.Unlock()
	if len(injectDown) != 0 {
		t.Errorf("injectDown 未清空: %v", injectDown)
	}
	if injectButtons != 0 || injectMods != 0 || injectSynthetic != 0 {
		t.Errorf("状态未清空: buttons=%02X mods=%02X synthetic=%02X",
			injectButtons, injectMods, injectSynthetic)
	}

	keyUps := 0
	for _, flags := range keyFlags {
		if flags&keyeventfKeyUp != 0 {
			keyUps++
		}
	}
	if keyUps < 2 {
		t.Errorf("KEY_UP 数量 = %d, want >= 2 (Ctrl + A): %v", keyUps, keyFlags)
	}
	sawLeftUp := false
	for _, flags := range mouseFlags {
		if flags == mouseeventfLeftUp {
			sawLeftUp = true
		}
	}
	if !sawLeftUp {
		t.Errorf("缺少左键 MOUSEUP: %v", mouseFlags)
	}
}
