package protocol

import (
	"bytes"
	"testing"
	"time"
)

// mustMarshal marshals a clipboard chunk or fails the test.
func (p *ClipboardPayload) mustMarshal(t *testing.T) []byte {
	t.Helper()
	b, err := p.Marshal()
	if err != nil {
		t.Fatalf("marshal clipboard chunk: %v", err)
	}
	return b
}

// newClipboardPair wires a sender and receiver through an in-memory pipe —
// the sender's emit feeds the receiver's HandleFrame directly.
func newClipboardPair() (sender, receiver *ClipboardSession) {
	receiver = NewClipboardSession(func(frameType uint8, payload []byte) error {
		return nil // receiver never sends clipboard frames back
	})
	sender = NewClipboardSession(func(frameType uint8, payload []byte) error {
		_, err := receiver.HandleFrame(payload)
		return err
	})
	return sender, receiver
}

func TestClipboardSessionSendReceiveLoop(t *testing.T) {
	sender, receiver := newClipboardPair()

	var delivered Clipboard
	receiver.OnClipboard = func(c Clipboard) { delivered = c }

	data := bytes.Repeat([]byte{0x5A}, 600) // 3 chunks at "text/plain" budget
	if err := sender.SendClipboard("text/plain", data); err != nil {
		t.Fatalf("SendClipboard: %v", err)
	}
	if !bytes.Equal(delivered.Data, data) || delivered.MIMEType != "text/plain" {
		t.Fatalf("delivered clipboard mismatch: mime=%q len=%d", delivered.MIMEType, len(delivered.Data))
	}
}

func TestClipboardSessionDuplicateFirstChunk(t *testing.T) {
	// Feed chunk 0 of a 2-chunk transfer twice; assembly must still complete
	// exactly once with correct bytes.
	mime := ""
	chunkBudget := MaxClipboardChunk(len(mime))
	data := bytes.Repeat([]byte{0x01}, chunkBudget+10) // 2 chunks

	chunks, err := SegmentClipboard(mime, data)
	if err != nil {
		t.Fatal(err)
	}
	receiver := NewClipboardSession(func(uint8, []byte) error { return nil })

	var count int
	receiver.OnClipboard = func(Clipboard) { count++ }

	for i := 0; i < 2; i++ { // feed chunk 0 twice
		payload, err := chunks[0].Marshal()
		if err != nil {
			t.Fatal(err)
		}
		if _, err := receiver.HandleFrame(payload); err != nil {
			t.Fatalf("feed 0 #%d: %v", i, err)
		}
	}
	payload, _ := chunks[1].Marshal()
	clip, err := receiver.HandleFrame(payload)
	if err != nil {
		t.Fatalf("feed 1: %v", err)
	}
	if clip == nil || !bytes.Equal(clip.Data, data) {
		t.Fatalf("clipboard mismatch: %+v", clip)
	}
	if count != 1 {
		t.Errorf("OnClipboard fired %d times, want 1", count)
	}
}

// A new transfer (FragIdx=0) supersedes an incomplete one; a non-zero index
// that does not match the active transfer is rejected.
func TestClipboardSessionSupersedeAndMismatch(t *testing.T) {
	receiver := NewClipboardSession(func(uint8, []byte) error { return nil })

	first := (&ClipboardPayload{MIMEType: "a/b", TotalSize: 10, FragTot: 3, FragIdx: 0, Data: []byte("12345")}).mustMarshal(t)
	if _, err := receiver.HandleFrame(first); err != nil {
		t.Fatal(err)
	}

	// Same transfer name but a different TotalSize, valid on its own:
	// rejected because it does not match the active transfer.
	stray := (&ClipboardPayload{MIMEType: "a/b", TotalSize: 99, FragTot: 3, FragIdx: 2, Data: []byte("x")}).mustMarshal(t)
	if _, err := receiver.HandleFrame(stray); err == nil {
		t.Error("chunk outside the active transfer must be rejected")
	}

	// A fresh FragIdx=0 with a different shape supersedes the old buffer.
	second := (&ClipboardPayload{MIMEType: "a/b", TotalSize: 4, FragTot: 1, FragIdx: 0, Data: []byte("abcd")}).mustMarshal(t)
	clip, err := receiver.HandleFrame(second)
	if err != nil {
		t.Fatalf("superseding transfer: %v", err)
	}
	if clip == nil || string(clip.Data) != "abcd" {
		t.Fatalf("superseding transfer delivered %+v", clip)
	}
}

func TestClipboardSessionTimeout(t *testing.T) {
	receiver := NewClipboardSession(func(uint8, []byte) error { return nil })

	current := time.Unix(0, 0)
	receiver.SetClock(func() time.Time { return current })

	half := (&ClipboardPayload{MIMEType: "a/b", TotalSize: 8, FragTot: 2, FragIdx: 0, Data: []byte("1234")}).mustMarshal(t)
	if _, err := receiver.HandleFrame(half); err != nil {
		t.Fatal(err)
	}

	current = current.Add(6 * time.Second) // past ClipboardTimeout
	secondHalf := (&ClipboardPayload{MIMEType: "a/b", TotalSize: 8, FragTot: 2, FragIdx: 1, Data: []byte("5678")}).mustMarshal(t)
	clip, err := receiver.HandleFrame(secondHalf)
	if err != nil {
		t.Fatalf("late chunk: %v", err)
	}
	if clip != nil {
		t.Error("expired transfer must not complete from the stale remainder")
	}
}
