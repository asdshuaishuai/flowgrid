package protocol

import (
	"bytes"
	"testing"
	"time"
)

// filePair wires a sender and receiver through in-memory pipes in both
// directions: sender.emit → receiver.HandleFrame, receiver.emit → sender.HandleFrame.
type filePair struct {
	sender   *FileSender
	receiver *FileReceiver

	offerAccepted bool
	completed     chan fileCompletion
	senderDone    chan error
}

type fileCompletion struct {
	offer FileOffer
	data  []byte
	err   error
}

func newFilePair() *filePair {
	p := &filePair{
		completed:  make(chan fileCompletion, 4),
		senderDone: make(chan error, 4),
	}
	p.receiver = NewFileReceiver(func(frameType uint8, payload []byte) error {
		return p.sender.HandleFrame(frameType, payload)
	})
	p.sender = NewFileSender(func(frameType uint8, payload []byte) error {
		_, err := p.receiver.HandleFrame(frameType, payload)
		return err
	})
	p.receiver.OnOffer = func(offer FileOffer) {
		p.offerAccepted = true
		_ = p.receiver.Accept(offer.TransferID) // errors surface via the done/completion channels
	}
	p.receiver.OnComplete = func(offer FileOffer, data []byte, err error) {
		p.completed <- fileCompletion{offer: offer, data: data, err: err}
	}
	p.sender.OnDone = func(id uint16, err error) {
		p.senderDone <- err
	}
	return p
}

func TestFileTransferFullLoop(t *testing.T) {
	p := newFilePair()

	data := make([]byte, 1000) // 5 chunks at 249 bytes
	for i := range data {
		data[i] = byte(i * 7)
	}
	id, err := p.sender.OfferFile("report.txt", data)
	if err != nil {
		t.Fatalf("OfferFile: %v", err)
	}
	if !p.offerAccepted {
		t.Fatal("offer was not auto-accepted")
	}

	// Pump until the sender reports completion.
	for i := 0; i < 100; i++ {
		n, err := p.sender.Pump(2)
		if err != nil {
			t.Fatalf("Pump: %v", err)
		}
		if n == 0 {
			break
		}
	}

	select {
	case err := <-p.senderDone:
		if err != nil {
			t.Fatalf("sender done with error: %v", err)
		}
	case <-time.After(time.Second):
		t.Fatal("sender never completed")
	}
	select {
	case c := <-p.completed:
		if c.err != nil {
			t.Fatalf("receiver failed: %v", c.err)
		}
		if !bytes.Equal(c.data, data) {
			t.Error("received file differs from sent file")
		}
		if c.offer.Name != "report.txt" || c.offer.TransferID != id {
			t.Errorf("offer metadata mismatch: %+v", c.offer)
		}
	case <-time.After(time.Second):
		t.Fatal("receiver never completed")
	}
}

func TestFileTransferRejected(t *testing.T) {
	p := newFilePair()
	p.receiver.OnOffer = func(offer FileOffer) {
		if err := p.receiver.Reject(offer.TransferID, FileRejectedUser); err != nil {
			t.Errorf("Reject: %v", err)
		}
	}

	_, err := p.sender.OfferFile("blocked.txt", []byte("secret"))
	if err != nil {
		t.Fatalf("OfferFile: %v", err)
	}

	select {
	case err := <-p.senderDone:
		if err == nil {
			t.Fatal("expected rejection error")
		}
	case <-time.After(time.Second):
		t.Fatal("sender never finished after rejection")
	}
}

func TestFileTransferAbortByReceiver(t *testing.T) {
	p := newFilePair()

	data := bytes.Repeat([]byte{0xAB}, 1000)
	if _, err := p.sender.OfferFile("big.bin", data); err != nil {
		t.Fatal(err)
	}
	if _, err := p.sender.Pump(1); err != nil { // one chunk, then abort
		t.Fatal(err)
	}
	if err := p.receiver.Abort(p.activeTransferID(), FileAbortUser); err != nil {
		t.Fatalf("Abort: %v", err)
	}

	select {
	case err := <-p.senderDone:
		if err == nil {
			t.Fatal("expected abort error on sender side")
		}
	case <-time.After(time.Second):
		t.Fatal("sender never finished after abort")
	}
	select {
	case c := <-p.completed:
		if c.err == nil {
			t.Fatal("expected failure on receiver side")
		}
	case <-time.After(time.Second):
		t.Fatal("receiver never reported the abort")
	}
}

// activeTransferID digs the current transfer id out of the receiver (test aid).
func (p *filePair) activeTransferID() uint16 {
	p.receiver.mu.Lock()
	defer p.receiver.mu.Unlock()
	if p.receiver.active != nil {
		return p.receiver.active.offer.TransferID
	}
	if p.receiver.offered != nil {
		return p.receiver.offered.offer.TransferID
	}
	return 0
}

// Regression: a stalled sender must not wedge the receiver. After the active
// transfer goes idle past FileIdleTimeout, a new FILE_OFFER supersedes it
// (the stale transfer fails locally) instead of being busy-rejected forever.
func TestFileReceiverStaleActiveExpires(t *testing.T) {
	p := newFilePair()
	current := time.Unix(0, 0)
	p.sender.SetClock(func() time.Time { return current })
	p.receiver.SetClock(func() time.Time { return current })

	if _, err := p.sender.OfferFile("stalled.bin", bytes.Repeat([]byte{1}, 1000)); err != nil {
		t.Fatal(err)
	}
	if _, err := p.sender.Pump(1); err != nil { // one chunk, then the sender stalls
		t.Fatal(err)
	}

	current = current.Add(11 * time.Second) // past FileIdleTimeout
	offered := make(chan FileOffer, 1)
	p.receiver.OnOffer = func(offer FileOffer) {
		offered <- offer
		if err := p.receiver.Accept(offer.TransferID); err != nil {
			t.Errorf("Accept after expiry: %v", err)
		}
	}
	if _, err := p.sender.OfferFile("fresh.txt", []byte("fresh data")); err != nil {
		t.Fatalf("new offer must not be busy-rejected after stale transfer expiry: %v", err)
	}
	select {
	case o := <-offered:
		if o.Name != "fresh.txt" {
			t.Errorf("presented offer: got %q, want fresh.txt", o.Name)
		}
	case <-time.After(time.Second):
		t.Fatal("receiver never presented the new offer")
	}
	// The stale transfer must have been reported as failed to the app.
	select {
	case c := <-p.completed:
		if c.err == nil {
			t.Error("stale transfer should be failed with a timeout error")
		}
	default:
		t.Fatal("stale transfer was silently dropped, no completion reported")
	}
}

func TestFileTransferOfferTimeout(t *testing.T) {
	p := newFilePair()
	p.receiver.OnOffer = func(FileOffer) {} // never accept

	current := time.Unix(0, 0)
	p.sender.SetClock(func() time.Time { return current })

	if _, err := p.sender.OfferFile("slow.txt", []byte("data")); err != nil {
		t.Fatal(err)
	}

	current = current.Add(31 * time.Second) // past FileOfferTimeout
	if _, err := p.sender.Pump(1); err == nil {
		t.Fatal("expected unacknowledged offer to time out")
	}
	select {
	case err := <-p.senderDone:
		if err == nil {
			t.Fatal("expected timeout error on sender done")
		}
	default:
		t.Fatal("sender should have reported the timeout")
	}
}

// §7.4: an unanswered offer is void after FileOfferTimeout. Accepting it late
// must fail rather than mark a sender-abandoned transfer active — otherwise
// r.active stays non-nil with a fresh lastMove and every new offer is declined
// as busy until FileIdleTimeout.
func TestFileTransferAcceptAfterOfferTimeout(t *testing.T) {
	p := newFilePair()
	p.receiver.OnOffer = func(FileOffer) {} // never accept automatically

	current := time.Unix(0, 0)
	p.sender.SetClock(func() time.Time { return current })
	p.receiver.SetClock(func() time.Time { return current })

	id, err := p.sender.OfferFile("slow.txt", []byte("data"))
	if err != nil {
		t.Fatal(err)
	}

	current = current.Add(31 * time.Second) // past FileOfferTimeout
	if err := p.receiver.Accept(id); err == nil {
		t.Fatal("accepting an offer past FileOfferTimeout must fail")
	}
}

// 迟到/重复的 ACK 是普通网络噪声（§7.4：TransferID 不匹配的帧一律静默丢弃），
// 不能报成错误让客户端弹「FILE_ACK for unknown transfer」：对端只是慢了一拍。
func TestFileTransferLateAckIsDropped(t *testing.T) {
	p := newFilePair()
	data := bytes.Repeat([]byte{0x5A}, 300) // 2 段，确认整路真的跑完
	id, err := p.sender.OfferFile("late.txt", data)
	if err != nil {
		t.Fatal(err)
	}
	for {
		n, err := p.sender.Pump(4)
		if err != nil {
			t.Fatalf("Pump: %v", err)
		}
		if n == 0 {
			break
		}
	}
	select {
	case err := <-p.senderDone:
		if err != nil {
			t.Fatalf("正常传输不该失败：%v", err)
		}
	case <-time.After(time.Second):
		t.Fatal("传输没跑完")
	}

	// 传输已终态：重复的完成确认、迟到的哈希不符、别人的 TransferID，全静默。
	for _, ack := range []*FileAck{
		{TransferID: id, Status: FileComplete},
		{TransferID: id, Status: FileHashMismatch},
		{TransferID: id + 3, Status: FileComplete},
		{TransferID: id + 3, Status: FileAccepted},
		{TransferID: id + 3, Status: FileRejectedUser},
	} {
		if err := p.sender.HandleFrame(FrameFileAck, ack.Marshal()); err != nil {
			t.Errorf("status=0x%02X 的迟到 ACK 应静默丢弃，却报了 %v", uint8(ack.Status), err)
		}
	}
	if n := len(p.senderDone); n != 0 {
		t.Errorf("静默丢弃不该触发 OnDone，却触发了 %d 次", n)
	}

	// 状态字节本身不认识仍是载荷错误，不能一并吞掉。
	bad := (&FileAck{TransferID: id, Status: FileAckStatus(0x7F)}).Marshal()
	if err := p.sender.HandleFrame(FrameFileAck, bad); err == nil {
		t.Error("未知的 FILE_ACK 状态应报错")
	}
}

// §7.4 的 FILE_ABORT 是「任意时刻」：接收方还没接受提议就中止（用户等不及），
// 发送方的 pending 也必须清掉——只清 active 的话，直到 FileOfferTimeout 之前
// 新的 OfferFile 一律报「another offer is pending」。
func TestFileTransferAbortWhileOffered(t *testing.T) {
	p := newFilePair()
	p.receiver.OnOffer = func(FileOffer) {} // 不自动接受

	id, err := p.sender.OfferFile("first.bin", []byte("data"))
	if err != nil {
		t.Fatal(err)
	}
	if err := p.receiver.Abort(id, FileAbortUser); err != nil {
		t.Fatalf("提议阶段中止失败：%v", err)
	}
	select {
	case err := <-p.senderDone:
		if err == nil {
			t.Fatal("提议阶段被中止也要报终态")
		}
	case <-time.After(time.Second):
		t.Fatal("发送方没收到中止")
	}
	if _, err := p.sender.OfferFile("second.bin", []byte("data2")); err != nil {
		t.Errorf("中止后应能立刻发起下一路，却报了 %v", err)
	}
}

func TestFileTransferSecondOfferWhilePending(t *testing.T) {
	p := newFilePair()
	p.receiver.OnOffer = func(FileOffer) {} // do not accept

	if _, err := p.sender.OfferFile("a.txt", []byte("one")); err != nil {
		t.Fatal(err)
	}
	if _, err := p.sender.OfferFile("b.txt", []byte("two")); err == nil {
		t.Fatal("second offer while one is pending must be rejected")
	}
}

func TestFileWireFormatRoundTrip(t *testing.T) {
	offer := &FileOffer{TransferID: 7, Size: 1234, Name: "img.png"}
	offer.SHA256 = [32]byte{1, 2, 3}
	wire, err := offer.Marshal()
	if err != nil {
		t.Fatal(err)
	}
	got, err := UnmarshalFileOffer(wire)
	if err != nil {
		t.Fatal(err)
	}
	if got.TransferID != 7 || got.Size != 1234 || got.Name != "img.png" || got.SHA256 != offer.SHA256 {
		t.Errorf("offer round trip mismatch: %+v", got)
	}

	ack := (&FileAck{TransferID: 7, Status: FileComplete}).Marshal()
	gotAck, err := UnmarshalFileAck(ack)
	if err != nil || gotAck.Status != FileComplete || gotAck.TransferID != 7 {
		t.Errorf("ack round trip: %+v err=%v", gotAck, err)
	}

	chunk := &FileChunk{TransferID: 7, ChunkIdx: 9, Data: []byte("payload")}
	wire, err = chunk.Marshal()
	if err != nil {
		t.Fatal(err)
	}
	gotChunk, err := UnmarshalFileChunk(wire)
	if err != nil || gotChunk.ChunkIdx != 9 || string(gotChunk.Data) != "payload" {
		t.Errorf("chunk round trip: %+v err=%v", gotChunk, err)
	}

	abort := (&FileAbort{TransferID: 7, Reason: FileAbortUser}).Marshal()
	gotAbort, err := UnmarshalFileAbort(abort)
	if err != nil || gotAbort.Reason != FileAbortUser {
		t.Errorf("abort round trip: %+v err=%v", gotAbort, err)
	}
}

func TestFileOfferValidation(t *testing.T) {
	if _, err := (&FileOffer{Size: 10, Name: "a/b.txt"}).Marshal(); err == nil {
		t.Error("path separator in name must be rejected")
	}
	if _, err := (&FileOffer{Size: 0, Name: "ok.txt"}).Marshal(); err == nil {
		t.Error("zero size must be rejected")
	}
	big := &FileOffer{Size: MaxFileSize + 1, Name: "big.bin"}
	if _, err := big.Marshal(); err == nil {
		t.Error("oversize must be rejected")
	}
	if _, err := (&FileOffer{Size: 1, Name: ""}).Marshal(); err == nil {
		t.Error("empty name must be rejected")
	}
	_, err := UnmarshalFileOffer([]byte{0x00})
	if err == nil {
		t.Error("short FILE_OFFER must be rejected")
	}
	if _, err := (&FileChunk{TransferID: 1, Data: make([]byte, MaxFileChunk+1)}).Marshal(); err == nil {
		t.Error("oversize chunk must be rejected")
	}
}

// 进度口报的是「至少交付过一次的字节数」，而且要只增不减——客户端直接拿它算
// 百分比、显示文件大小、估剩余时间（FilesPanel::pollSessions）。
//
// 早先报的是 chunk 边界（nextChunk*MaxFileChunk）乘出来的数：末段不满 249 B，
// 1000 B 的文件被报成 1245 B；补发 pass 又把 nextChunk 归零，进度条于是从
// 100% 掉回 20%，再爬回 100%，来回三次。
func TestFileSenderProgressIsRealAndMonotonic(t *testing.T) {
	s := NewFileSender(func(uint8, []byte) error { return nil }) // 只出帧，不回 ACK
	var reports [][2]int
	s.OnProgress = func(_ uint16, sent, total int) {
		reports = append(reports, [2]int{sent, total})
	}

	data := make([]byte, 1000) // 5 段（249 B/段），末段只有 4 B
	id, err := s.OfferFile("report.txt", data)
	if err != nil {
		t.Fatalf("OfferFile: %v", err)
	}
	// 模拟对端接受（FILE_ACK accepted），让 active 立起来。
	if err := s.HandleFrame(FrameFileAck,
		(&FileAck{TransferID: id, Status: FileAccepted}).Marshal()); err != nil {
		t.Fatalf("accept: %v", err)
	}

	// 一遍 5 段 + 默认 2 次补发 = 15 段；每次只发 1 段，逼出多次 pass 边界。
	for i := 0; i < 20; i++ {
		if _, err := s.Pump(1); err != nil {
			t.Fatalf("Pump: %v", err)
		}
	}
	if len(reports) == 0 {
		t.Fatal("OnProgress 一次都没报")
	}

	prev := -1
	for i, r := range reports {
		if r[1] != len(data) {
			t.Errorf("第 %d 次进度 total=%d，应为文件真实大小 %d", i+1, r[1], len(data))
		}
		if r[0] < prev {
			t.Errorf("第 %d 次进度倒退：sent %d -> %d", i+1, prev, r[0])
		}
		if r[0] > len(data) {
			t.Errorf("第 %d 次进度超出文件大小：sent %d > %d", i+1, r[0], len(data))
		}
		prev = r[0]
	}
	if prev != len(data) {
		t.Errorf("跑完一遍应报到 100%%，最后一次却只到 %d/%d", prev, len(data))
	}
}
