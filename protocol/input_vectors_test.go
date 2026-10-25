package protocol_test

import (
	"bytes"
	"encoding/hex"
	"encoding/json"
	"os"
	"path/filepath"
	"testing"

	"github.com/flowgrid/protocol"
)

// 向量文件必须与生成器输出完全一致。
//
// 为什么要有这条：向量文件是**提交进仓库**的产物，各端测试读它来钉行为。
// 如果改了协议行为却忘了重新生成，各端测试会拿旧期望去比——要么全绿
// （假的），要么全红（白忙）。这条测试把「忘了重新生成」变成一次明确的失败。
func TestInputVectorsFileUpToDate(t *testing.T) {
	want, err := json.MarshalIndent(protocol.BuildInputVectors(), "", "  ")
	if err != nil {
		t.Fatalf("生成向量失败: %v", err)
	}
	want = append(want, '\n')

	path := filepath.Join("testdata", "input_vectors.json")
	got, err := os.ReadFile(path)
	if err != nil {
		t.Fatalf("读 %s 失败: %v（先跑 go run ./cmd/inputvectors -w %s）", path, err, path)
	}
	if !bytes.Equal(got, want) {
		t.Errorf("%s 与生成器输出不一致——协议行为变了但向量没重新生成。\n"+
			"跑：go run ./cmd/inputvectors -w %s", path, path)
	}
}

// 向量文件里的每个用例都必须自洽：期望的载荷能被协议层解回同样的字段。
//
// 这条是向量文件自身的体检——生成器算错了（比如把 DeltaX/DeltaY 写反了），
// 这里就会红，而不是等到某个客户端的测试莫名其妙地红。
func TestInputVectorsSelfConsistent(t *testing.T) {
	vectors := protocol.BuildInputVectors()
	if len(vectors.Cases) == 0 {
		t.Fatal("向量文件里一个用例都没有")
	}
	for _, c := range vectors.Cases {
		if len(c.Events) != len(c.Expect) {
			t.Errorf("用例 %s：事件 %d 个但期望 %d 个", c.Name, len(c.Events), len(c.Expect))
			continue
		}
		for _, want := range c.Expect {
			payload, err := hex.DecodeString(want.PayloadHex)
			if err != nil {
				t.Errorf("用例 %s：载荷 hex 非法: %v", c.Name, err)
				continue
			}
			ev, err := protocol.DecodeInputFrame(frameTypeOf(want.FrameType), payload,
				platformOf(c.To), platformOf(c.To), nil, protocol.ContextGlobal)
			if err != nil {
				t.Errorf("用例 %s：载荷解不回事件: %v", c.Name, err)
				continue
			}
			switch want.FrameType {
			case "key_down":
				if ev.KeyCode != want.Key || ev.Modifiers != want.Modifiers {
					t.Errorf("用例 %s：key_down 期望 key=0x%04x mods=0x%02x，解出 key=0x%04x mods=0x%02x",
						c.Name, want.Key, want.Modifiers, ev.KeyCode, ev.Modifiers)
				}
			case "key_up":
				if ev.KeyCode != want.Key {
					t.Errorf("用例 %s：key_up 期望 key=0x%04x，解出 0x%04x", c.Name, want.Key, ev.KeyCode)
				}
			case "mouse_move":
				if ev.DeltaX != want.DeltaX || ev.DeltaY != want.DeltaY {
					t.Errorf("用例 %s：mouse_move 期望 (%d,%d)，解出 (%d,%d)",
						c.Name, want.DeltaX, want.DeltaY, ev.DeltaX, ev.DeltaY)
				}
			case "mouse_button":
				if ev.Button != want.Button || ev.Pressed != want.Pressed {
					t.Errorf("用例 %s：mouse_button 期望 button=%d pressed=%t，解出 button=%d pressed=%t",
						c.Name, want.Button, want.Pressed, ev.Button, ev.Pressed)
				}
			case "scroll":
				if ev.DeltaX != want.DeltaX || ev.DeltaY != want.DeltaY {
					t.Errorf("用例 %s：scroll 期望 (%d,%d)，解出 (%d,%d)",
						c.Name, want.DeltaX, want.DeltaY, ev.DeltaX, ev.DeltaY)
				}
			}
		}
	}
}

func frameTypeOf(name string) uint8 {
	switch name {
	case "key_down":
		return protocol.FrameKeyDown
	case "key_up":
		return protocol.FrameKeyUp
	case "mouse_move":
		return protocol.FrameMouseMove
	case "mouse_button":
		return protocol.FrameMouseButton
	case "scroll":
		return protocol.FrameScroll
	default:
		return 0
	}
}

func platformOf(name string) protocol.Platform {
	switch name {
	case "macos":
		return protocol.PlatformMacOS
	case "windows":
		return protocol.PlatformWindows
	case "linux":
		return protocol.PlatformLinux
	case "android":
		return protocol.PlatformAndroid
	case "ios":
		return protocol.PlatformIOS
	default:
		return protocol.PlatformLinux
	}
}
