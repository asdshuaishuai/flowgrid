package main

import (
	"bytes"
	"sync"
	"testing"

	"github.com/flowgrid/protocol"
)

// FFI exports are called from multiple OS threads (Qt + tokio); the handle
// registries must survive concurrent put/get/remove without data races.
// Run with -race to verify.
func TestRegistryConcurrentAccess(t *testing.T) {
	var reg registry[*protocol.LatencyMonitor]

	const workers = 8
	const iters = 200

	var wg sync.WaitGroup
	for w := 0; w < workers; w++ {
		wg.Add(1)
		go func(w int) {
			defer wg.Done()
			for i := 0; i < iters; i++ {
				id := reg.put(protocol.NewLatencyMonitor(4))
				if _, ok := reg.get(id); !ok {
					t.Errorf("handle %d missing right after put", id)
					return
				}
				reg.remove(id)
				if _, ok := reg.get(id); ok {
					t.Errorf("handle %d still present after remove", id)
					return
				}
			}
		}(w)
	}
	wg.Wait()

	reg.mu.Lock()
	n := len(reg.items)
	reg.mu.Unlock()
	if n != 0 {
		t.Errorf("registry leaked %d entries", n)
	}
}

func TestKeymapFFILookupMatchesGo(t *testing.T) {
	kmf, err := protocol.ParseKeyMap([]byte("rules:\n- fromOS: macos\n  toOS: windows\n  fromKey: 6\n  toKey: 7\n  context: global\n"))
	if err != nil {
		t.Fatalf("ParseKeyMap: %v", err)
	}
	table := protocol.NewKeyMapTable(kmf)

	key, mods, found := table.Lookup(protocol.PlatformMacOS, protocol.PlatformWindows, 6, 0, protocol.ContextGlobal)
	if !found || key != 7 || mods != 0 {
		t.Errorf("lookup: got key=%d mods=%d found=%v, want key=7 found=true", key, mods, found)
	}
}

// 会话出帧队列：未声明 sink 时必须报错，而不是把帧丢掉又报成功——旧实现
// 把 emitFrame 接到一个 return nil 的空壳 sink 上，正是「假成功」的根源。
func TestSessionOutboxPollSemantics(t *testing.T) {
	fg_session_set_sink(0)
	defer fg_session_set_sink(0)

	if err := emitFrame(protocol.FrameClipboard, []byte{1, 2, 3}); err == nil {
		t.Fatal("未声明 sink 时 emitFrame 必须报错，而不是静默丢帧")
	}

	fg_session_set_sink(1)
	in := []byte{1, 2, 3}
	if err := emitFrame(protocol.FrameClipboard, in); err != nil {
		t.Fatalf("emitFrame: %v", err)
	}
	in[0] = 9 // 入队应拷贝：改写入参不能影响已入队的帧

	ft, payload, ok := pollSessionOut()
	if !ok || ft != protocol.FrameClipboard || !bytes.Equal(payload, []byte{1, 2, 3}) {
		t.Fatalf("poll: type=%#x payload=%v ok=%v", ft, payload, ok)
	}
	if _, _, ok := pollSessionOut(); ok {
		t.Fatal("队列空时 poll 应返回 ok=false")
	}
}

// 队列满时报错（不能阻塞，更不能丢帧报成功）；撤下 sink 会清空队列。
func TestSessionOutboxFullAndReset(t *testing.T) {
	fg_session_set_sink(0)
	fg_session_set_sink(1)
	defer fg_session_set_sink(0)

	for i := 0; i < sessionOutMax; i++ {
		if err := emitFrame(protocol.FrameClipboard, []byte{byte(i)}); err != nil {
			t.Fatalf("第 %d 帧入队失败: %v", i, err)
		}
	}
	if err := emitFrame(protocol.FrameClipboard, []byte{0}); err == nil {
		t.Fatal("队列满时必须报错，不能阻塞或丢帧报成功")
	}

	fg_session_set_sink(0)
	if _, _, ok := pollSessionOut(); ok {
		t.Fatal("撤下 sink 后队列应已清空")
	}
}
