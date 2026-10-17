package main

import (
	"bytes"
	"strconv"
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

// feedClipboard 把一条单段 CLIPBOARD 载荷灌进会话（走 Go 方法，绕开出参编组），
// 完成重组时包装层的 OnClipboard 会把它排进待取队列。
func feedClipboard(t *testing.T, h uintptr, mime, body string) {
	t.Helper()
	cs, ok := clipSessions.get(h)
	if !ok {
		t.Fatal("会话句柄取不到")
	}
	payload, err := (&protocol.ClipboardPayload{
		FragIdx: 0, FragTot: 1, MIMEType: mime, Data: []byte(body),
	}).Marshal()
	if err != nil {
		t.Fatalf("marshal %q: %v", body, err)
	}
	if _, err := cs.sess.HandleFrame(payload); err != nil {
		t.Fatalf("HandleFrame %q: %v", body, err)
	}
}

// 取出来的那条必须带着自己的 MIME 与数据。早先 fg_clipboard_take_pending 只把
// out*Len 填 0：C 侧 GoBuffer 按 0 长度切片，每一份入站剪贴板都被读成「空 MIME +
// 空数据」——面板上是一条 0 B、没有类型的记录，对端传来的内容全丢。
func TestClipboardTakePendingCarriesPayload(t *testing.T) {
	h := fg_clipboard_session_new()
	defer fg_clipboard_session_free(h)
	feedClipboard(t, h, "text/plain", "hello 剪贴板")

	got, ok := takePendingClipboard(h)
	if !ok {
		t.Fatal("takePendingClipboard 取不到刚投递的条目")
	}
	if got.MIMEType != "text/plain" {
		t.Errorf("MIME 不对：%q", got.MIMEType)
	}
	if string(got.Data) != "hello 剪贴板" {
		t.Errorf("数据不对：%q", got.Data)
	}
}

// 下行剪贴板要按到达顺序排队：客户端取走一条就往历史头部放一条，顺序错了
// 「当前剪贴板」显示的就是最旧的那条。队列空时必须报 false，否则客户端 drain
// 用的 while 循环转不停。
func TestClipboardTakePendingKeepsArrivalOrder(t *testing.T) {
	h := fg_clipboard_session_new()
	defer fg_clipboard_session_free(h)

	want := []string{"第一条", "第二条", "第三条"}
	for _, s := range want {
		feedClipboard(t, h, "text/plain", s)
	}

	for i, w := range want {
		got, ok := takePendingClipboard(h)
		if !ok {
			t.Fatalf("第 %d 条取不到（队列提前空了）", i+1)
		}
		if string(got.Data) != w {
			t.Errorf("第 %d 条顺序不对：拿到 %q，想要 %q", i+1, got.Data, w)
		}
	}
	if _, ok := takePendingClipboard(h); ok {
		t.Error("取空后仍返回条目，客户端的 drain 循环会转不停")
	}
}

// 队列到顶丢最旧的一条：客户端历史最多留 200 条，超了也展示不出来，但队列不能
// 无限长——C 侧不取，它就是一个一直涨的内存口子。
func TestClipboardInboxDropsOldestWhenFull(t *testing.T) {
	h := fg_clipboard_session_new()
	defer fg_clipboard_session_free(h)

	const overflow = 3
	for i := 0; i < clipInboxMax+overflow; i++ {
		feedClipboard(t, h, "text/plain", strconv.Itoa(i))
	}

	cs, ok := clipSessions.get(h)
	if !ok {
		t.Fatal("会话句柄取不到")
	}
	cs.mu.Lock()
	n := len(cs.pending)
	first := string(cs.pending[0].Data)
	cs.mu.Unlock()

	if n != clipInboxMax {
		t.Errorf("队列应有 %d 条，实际 %d", clipInboxMax, n)
	}
	if first != strconv.Itoa(overflow) {
		t.Errorf("该丢最旧的 %d 条，队首却是 %q", overflow, first)
	}
}
