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
	if pending, dropped := clipboardPendingStats(h); pending != clipInboxMax || dropped != overflow {
		t.Errorf("溢出计数不对：pending=%d dropped=%d，想要 %d/%d", pending, dropped, clipInboxMax, overflow)
	}
}

// 历史是只读、有界、非破坏性的：take_pending 取走不影响历史，同一索引可反复
// 读；超过 50 条只挤掉最旧的。UI 的「最近同步」列表和设备页计数徽章可以各自
// 读同一份数据，不会互相把对方的数据取空。
func TestClipboardHistoryIsBoundedAndNonDestructive(t *testing.T) {
	h := fg_clipboard_session_new()
	defer fg_clipboard_session_free(h)

	total := clipHistoryMax + 5
	for i := 0; i < total; i++ {
		feedClipboard(t, h, "text/plain", strconv.Itoa(i))
	}

	if got := clipboardHistoryCount(h); got != clipHistoryMax {
		t.Fatalf("历史应保留 %d 条，实际 %d", clipHistoryMax, got)
	}
	first, ok := clipboardHistoryEntry(h, 0)
	if !ok {
		t.Fatal("取不到最旧的一条历史")
	}
	if string(first.clip.Data) != strconv.Itoa(total-clipHistoryMax) {
		t.Errorf("最旧历史应为第 %d 条，实际 %q", total-clipHistoryMax, first.clip.Data)
	}
	last, ok := clipboardHistoryEntry(h, clipHistoryMax-1)
	if !ok {
		t.Fatal("取不到最新的一条历史")
	}
	if string(last.clip.Data) != strconv.Itoa(total-1) {
		t.Errorf("最新历史应为 %q，实际 %q", strconv.Itoa(total-1), last.clip.Data)
	}
	if first.seq >= last.seq {
		t.Errorf("seq 应递增：first=%d last=%d", first.seq, last.seq)
	}
	if first.receivedAt.IsZero() || last.receivedAt.IsZero() {
		t.Error("历史必须带重组完成时间戳")
	}

	// 取走 pending 不影响历史；重复读同一索引结果一致。
	if _, ok := takePendingClipboard(h); !ok {
		t.Fatal("pending 队列不应为空")
	}
	if got := clipboardHistoryCount(h); got != clipHistoryMax {
		t.Errorf("take_pending 后历史被破坏：%d 条", got)
	}
	again, ok := clipboardHistoryEntry(h, 0)
	if !ok || again.seq != first.seq || string(again.clip.Data) != string(first.clip.Data) {
		t.Errorf("同一索引第二次读取不一致：%+v vs %+v", first, again)
	}

	fg_clipboard_history_clear(h)
	if got := clipboardHistoryCount(h); got != 0 {
		t.Errorf("清空后历史应为 0，实际 %d", got)
	}
	if pending, _ := clipboardPendingStats(h); pending == 0 {
		t.Error("清空历史不应连带清掉 pending 队列")
	}
}

// 文件接收侧的 offer / 完成事件也要排队：两个文件在同一个轮询周期完成时，
// 前一个的 data 不能像单槽位时代那样被覆盖丢掉。溢出时丢最旧，并把次数暴露
// 给客户端。
func TestFileReceiverQueuesEventsAndReportsOverflow(t *testing.T) {
	h := fg_file_receiver_new()
	defer fg_file_receiver_free(h)
	fr, ok := fileReceivers.get(h)
	if !ok {
		t.Fatal("接收器句柄取不到")
	}

	const overflow = 2
	for i := 0; i < fileInboxMax+overflow; i++ {
		fr.sess.OnOffer(protocol.FileOffer{
			TransferID: uint16(i + 1),
			Name:       "offer-" + strconv.Itoa(i),
			Size:       uint64(i + 1),
		})
	}
	fr.mu.Lock()
	offerCount, droppedOffers := len(fr.offers), fr.droppedOffers
	fr.mu.Unlock()
	if offerCount != fileInboxMax || droppedOffers != overflow {
		t.Errorf("offer 队列应有 %d 条/丢 %d，实际 %d/%d", fileInboxMax, overflow, offerCount, droppedOffers)
	}
	for i := overflow; i < fileInboxMax+overflow; i++ {
		o, ok := fr.takeOffer()
		if !ok {
			t.Fatalf("第 %d 个 offer 取不到", i)
		}
		if o.TransferID != uint16(i+1) {
			t.Errorf("offer 顺序不对：拿到 ID=%d，想要 %d", o.TransferID, i+1)
		}
	}
	if _, ok := fr.takeOffer(); ok {
		t.Error("offer 队列取空后仍返回条目")
	}

	for i := 0; i < fileInboxMax+overflow; i++ {
		data := []byte{byte(i)}
		fr.sess.OnComplete(protocol.FileOffer{
			TransferID: uint16(i + 1),
			Name:       "done-" + strconv.Itoa(i),
			Size:       uint64(len(data)),
		}, data, nil)
	}
	fr.mu.Lock()
	completeCount, droppedCompletes := len(fr.completes), fr.droppedCompletes
	fr.mu.Unlock()
	if completeCount != fileInboxMax || droppedCompletes != overflow {
		t.Errorf("完成队列应有 %d 条/丢 %d，实际 %d/%d", fileInboxMax, overflow, completeCount, droppedCompletes)
	}
	for i := overflow; i < fileInboxMax+overflow; i++ {
		c, ok := fr.takeComplete()
		if !ok {
			t.Fatalf("第 %d 个完成事件取不到", i)
		}
		if c.offer.TransferID != uint16(i+1) {
			t.Errorf("完成事件顺序不对：拿到 ID=%d，想要 %d", c.offer.TransferID, i+1)
		}
		if len(c.data) != 1 || c.data[0] != byte(i) {
			t.Errorf("第 %d 个文件数据被覆盖：%v", i, c.data)
		}
	}
	if _, ok := fr.takeComplete(); ok {
		t.Error("完成队列取空后仍返回条目")
	}
}

// 轮询进度是非破坏性的：可以每帧读，终态只有 take_complete 才消费；取空后
// 状态回到空闲，下一路不会带着上一路的 100%。
func TestFileReceiverPollProgressIsNonDestructive(t *testing.T) {
	h := fg_file_receiver_new()
	defer fg_file_receiver_free(h)
	fr, ok := fileReceivers.get(h)
	if !ok {
		t.Fatal("接收器句柄取不到")
	}

	offer := protocol.FileOffer{TransferID: 7, Name: "progress.bin", Size: 100}
	fr.sess.OnProgress(offer, 40, 100)
	if state, got, total, code := fr.progressSnapshot(); state != fileReceiverActive || got != 40 || total != 100 || code != 0 {
		t.Fatalf("进行中快照不对：state=%d received=%d total=%d code=%d", state, got, total, code)
	}

	fr.sess.OnComplete(offer, make([]byte, 100), nil)
	for i := 0; i < 2; i++ {
		state, got, total, code := fr.progressSnapshot()
		if state != fileReceiverDone || got != 100 || total != 100 || code != 0 {
			t.Fatalf("第 %d 次完成快照不对：state=%d received=%d total=%d code=%d", i+1, state, got, total, code)
		}
	}
	if _, ok := fr.takeComplete(); !ok {
		t.Fatal("完成事件被轮询吃掉了")
	}
	if state, _, _, _ := fr.progressSnapshot(); state != fileReceiverIdle {
		t.Errorf("取走终态后应回到空闲，实际 state=%d", state)
	}

	// 失败终态：错误码要能通过轮询拿到，且 take_complete 仍交出事件。
	fr.sess.OnComplete(offer, nil, protocol.NewError(protocol.ErrTransferTimeout, "stalled"))
	if state, _, _, code := fr.progressSnapshot(); state != fileReceiverFailed || code != int(protocol.ErrTransferTimeout) {
		t.Fatalf("失败快照不对：state=%d code=%d", state, code)
	}
	c, ok := fr.takeComplete()
	if !ok || c.errCode != int(protocol.ErrTransferTimeout) {
		t.Errorf("失败事件取不到或错误码丢失：%+v ok=%v", c, ok)
	}
}

// 终态报一次就要把随行的进度一起清掉。留着的话，下一路传输在客户端眼里是从
// 上一路的 100% 开始的——客户端只能按「第一条进行中的条目」匹配，分不出这是
// 旧数字（条目在发起瞬间就显示满进度）。
func TestFileSenderSnapshotConsumesTerminalState(t *testing.T) {
	fs := &fileSender{state: fileSenderDone, sent: 1000, total: 1000}

	state, sent, total, _ := fs.snapshot()
	if state != fileSenderDone || sent != 1000 || total != 1000 {
		t.Errorf("第一次轮询要报出终态本身：state=%d sent=%d total=%d", state, sent, total)
	}

	state, sent, total, _ = fs.snapshot()
	if state != fileSenderIdle {
		t.Errorf("终态只报一次，第二次应为空闲，实际 state=%d", state)
	}
	if sent != 0 || total != 0 {
		t.Errorf("上一路的进度应随终态一起清掉，实际 sent=%d total=%d", sent, total)
	}

	// 失败终态同理：否则下一路会带着上一路失败时的错误码。
	fs.mu.Lock()
	fs.state, fs.errCode, fs.sent, fs.total = fileSenderFailed, int(protocol.ErrTransferTimeout), 12, 34
	fs.mu.Unlock()
	if _, _, _, code := fs.snapshot(); code != int(protocol.ErrTransferTimeout) {
		t.Errorf("失败要带上错误码，实际 %d", code)
	}
	if _, sent, total, code := fs.snapshot(); sent != 0 || total != 0 || code != 0 {
		t.Errorf("失败终态也应消费干净，实际 sent=%d total=%d code=%d", sent, total, code)
	}
}
