package protocol

import (
	"bytes"
	"testing"
	"time"
)

// Deterministic "bad network" tests: first delivery of each chunk is lost,
// retransmissions arrive. Proves the resend/dedup design recovers loss with
// zero wire changes.

// TestClipboardSessionLossyNetwork: pass 1 loses every chunk, pass 2 delivers
// all of them. The reassembled clipboard must still arrive byte-exact.
func TestClipboardSessionLossyNetwork(t *testing.T) {
	receiver := NewClipboardSession(func(uint8, []byte) error { return nil })
	var delivered *Clipboard
	receiver.OnClipboard = func(c Clipboard) { cc := c; delivered = &cc }

	dropped := map[int]bool{}
	sender := NewClipboardSession(func(_ uint8, payload []byte) error {
		chunk, err := UnmarshalClipboard(payload)
		if err != nil {
			return err
		}
		if !dropped[int(chunk.FragIdx)] {
			dropped[int(chunk.FragIdx)] = true
			return nil // first delivery of this chunk is lost
		}
		_, err = receiver.HandleFrame(payload)
		return err
	})

	data := make([]byte, 3000)
	for i := range data {
		data[i] = byte(i)
	}
	if err := sender.SendClipboardPasses("text/plain", data, 3); err != nil {
		t.Fatalf("SendClipboardPasses: %v", err)
	}
	if delivered == nil || !bytes.Equal(delivered.Data, data) {
		t.Fatalf("clipboard not recovered under first-pass loss: %+v", delivered)
	}
}

// Out-of-order arrival is buffered by index - but on a transport where the
// first pass can arrive fully reversed, only FragIdx=0 may open the buffer
// (no transfer id exists to tell a late duplicate from a restart), so the
// sender must run at least two passes. Pass 1 (reversed) seeds nothing but
// orphan-drops; pass 2 (in order) assembles byte-exact.
func TestClipboardSessionReorderedChunks(t *testing.T) {
	receiver := NewClipboardSession(func(uint8, []byte) error { return nil })

	mime := "text/plain"
	data := bytes.Repeat([]byte{0x77}, MaxClipboardChunk(len(mime))*2+17)
	chunks, err := SegmentClipboard(mime, data)
	if err != nil {
		t.Fatal(err)
	}

	var delivered *Clipboard
	receiver.OnClipboard = func(c Clipboard) { cc := c; delivered = &cc }

	feed := func(order []ClipboardPayload) {
		for i := range order {
			payload, err := order[i].Marshal()
			if err != nil {
				t.Fatal(err)
			}
			if _, err := receiver.HandleFrame(payload); err != nil {
				t.Fatalf("chunk %d: %v", i, err)
			}
		}
	}
	reversed := []ClipboardPayload{chunks[2], chunks[1], chunks[0]}
	feed(reversed) // pass 1: reversed
	if delivered != nil {
		t.Fatal("pass 1 must not assemble from reversed orphans")
	}
	feed(chunks) // pass 2: in order
	if delivered == nil || !bytes.Equal(delivered.Data, data) {
		t.Fatal("reordered chunks failed to assemble after second pass")
	}
}

// An orphan chunk (non-zero index, no active buffer) is ordinary noise and
// must be dropped silently - no error, no ghost buffer.
func TestClipboardSessionOrphanDropped(t *testing.T) {
	receiver := NewClipboardSession(func(uint8, []byte) error { return nil })
	orphan := (&ClipboardPayload{MIMEType: "a/b", TotalSize: 5, FragTot: 2, FragIdx: 1, Data: []byte("x")}).mustMarshal(t)
	clip, err := receiver.HandleFrame(orphan)
	if err != nil {
		t.Fatalf("orphan chunk must be dropped silently, got error: %v", err)
	}
	if clip != nil {
		t.Error("orphan chunk must not assemble anything")
	}
	// And the orphan must not have opened a ghost buffer: a real transfer
	// started right after must complete normally.
	first := (&ClipboardPayload{MIMEType: "a/b", TotalSize: 4, FragTot: 1, FragIdx: 0, Data: []byte("abcd")}).mustMarshal(t)
	clip, err = receiver.HandleFrame(first)
	if err != nil || clip == nil || string(clip.Data) != "abcd" {
		t.Fatalf("real transfer after orphan: clip=%+v err=%v", clip, err)
	}
}

// TestFileTransferResendRecoversLoss: the pipe drops the first delivery of
// every even-indexed chunk. Pump's resend passes must repair the loss and the
// receiver must deliver the verified file.
func TestFileTransferResendRecoversLoss(t *testing.T) {
	var snd *FileSender
	rcv := NewFileReceiver(func(frameType uint8, payload []byte) error {
		return snd.HandleFrame(frameType, payload)
	})
	seen := map[uint32]bool{}
	snd = NewFileSender(func(frameType uint8, payload []byte) error {
		if frameType == FrameFileChunk {
			c, err := UnmarshalFileChunk(payload)
			if err != nil {
				return err
			}
			if c.ChunkIdx%2 == 0 && !seen[c.ChunkIdx] {
				seen[c.ChunkIdx] = true
				return nil // first delivery of even chunks is lost
			}
		}
		_, err := rcv.HandleFrame(frameType, payload)
		return err
	})

	done := make(chan error, 1)
	snd.OnDone = func(_ uint16, err error) { done <- err }
	completed := make(chan fileCompletion, 1)
	rcv.OnOffer = func(offer FileOffer) {
		if err := rcv.Accept(offer.TransferID); err != nil {
			t.Errorf("Accept: %v", err)
		}
	}
	rcv.OnComplete = func(offer FileOffer, data []byte, err error) {
		completed <- fileCompletion{offer: offer, data: data, err: err}
	}

	data := make([]byte, 1000) // 5 chunks, indices 0..4; 0,2,4 lost on first pass
	for i := range data {
		data[i] = byte(i * 13)
	}
	if _, err := snd.OfferFile("lossy.bin", data); err != nil {
		t.Fatal(err)
	}
	for i := 0; i < 100; i++ {
		n, err := snd.Pump(2)
		if err != nil {
			t.Fatalf("Pump: %v", err)
		}
		if n == 0 {
			break
		}
	}

	select {
	case err := <-done:
		if err != nil {
			t.Fatalf("sender failed: %v", err)
		}
	case <-time.After(time.Second):
		t.Fatal("sender never completed despite resend passes")
	}
	select {
	case c := <-completed:
		if c.err != nil {
			t.Fatalf("receiver failed: %v", c.err)
		}
		if !bytes.Equal(c.data, data) {
			t.Error("delivered file differs from sent file")
		}
	case <-time.After(time.Second):
		t.Fatal("receiver never completed")
	}
}

// Late/unknown chunks after completion are network noise: dropped silently,
// no error, no state corruption.
func TestFileTransferLateChunkDroppedSilently(t *testing.T) {
	p := newFilePair()
	if _, err := p.sender.OfferFile("done.bin", bytes.Repeat([]byte{9}, 1000)); err != nil {
		t.Fatal(err)
	}
	for i := 0; i < 100; i++ {
		if n, err := p.sender.Pump(4); err != nil || n == 0 {
			break
		}
	}
	<-p.senderDone

	// Replay a chunk for the finished transfer id.
	id := p.activeTransferID()
	payload, err := (&FileChunk{TransferID: id, ChunkIdx: 0, Data: bytes.Repeat([]byte{1}, 10)}).Marshal()
	if err != nil {
		t.Fatal(err)
	}
	if _, err := p.receiver.HandleFrame(FrameFileChunk, payload); err != nil {
		t.Fatalf("late chunk after completion must be dropped silently, got: %v", err)
	}
}
