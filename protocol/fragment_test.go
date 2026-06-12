package protocol

import (
	"testing"
)

func TestFragmentFrameNoSplit(t *testing.T) {
	frame := &HIDFrame{
		Header: FrameHeader{
			FrameType: FrameKeyDown,
			Sequence:  1,
			Timestamp: 100,
		},
		Payload: []byte{0x00, 0x04, 0x01}, // 3 bytes, well under MTU
	}
	frags := FragmentFrame(frame, DefaultMTU)
	if frags != nil {
		t.Error("small frame should not be fragmented")
	}
}

func TestFragmentFrameSplit(t *testing.T) {
	// Create a large payload (clipboard)
	bigData := make([]byte, 3000)
	for i := range bigData {
		bigData[i] = byte(i % 256)
	}
	payload := &ClipboardPayload{MIMEType: "text/plain", Data: bigData}
	payloadBytes, _ := payload.Marshal()
	frame := &HIDFrame{
		Header: FrameHeader{
			FrameType: FrameClipboard,
			Sequence:  42,
			Timestamp: 999,
		},
		Payload: payloadBytes,
	}

	frags := FragmentFrame(frame, 100) // small MTU to force fragmentation
	if frags == nil {
		t.Fatal("large frame should be fragmented")
	}
	if len(frags) < 2 {
		t.Errorf("expected multiple fragments, got %d", len(frags))
	}

	// Verify each fragment has a valid header
	for i, frag := range frags {
		hdr, err := UnmarshalFragHeader(frag)
		if err != nil {
			t.Fatalf("frag %d header: %v", i, err)
		}
		if hdr.FragIdx != uint8(i) {
			t.Errorf("frag %d: FragIdx=%d", i, hdr.FragIdx)
		}
		if hdr.FragTot != uint8(len(frags)) {
			t.Errorf("frag %d: FragTot=%d, want %d", i, hdr.FragTot, len(frags))
		}
	}
}

func TestFragmentReassembly(t *testing.T) {
	reasm := NewFragmentReassembler()

	// Create a frame to fragment
	bigData := make([]byte, 500)
	for i := range bigData {
		bigData[i] = byte(i)
	}
	frame := &HIDFrame{
		Header: FrameHeader{
			FrameType: FrameClipboard,
			Sequence:  7,
			Timestamp: 12345,
		},
		Payload: bigData,
	}

	frags := FragmentFrame(frame, 100)
	if frags == nil {
		t.Fatal("should fragment")
	}

	// Set the same FragSeq for all fragments
	for _, frag := range frags {
		// FragSeq is at bytes 0-1
		frag[0] = 0x00
		frag[1] = 0x01 // FragSeq = 1
	}

	// Feed all but last
	for i := 0; i < len(frags)-1; i++ {
		result, err := reasm.Feed(frags[i])
		if err != nil {
			t.Fatalf("Feed frag %d: %v", i, err)
		}
		if result != nil {
			t.Errorf("frag %d: should not be complete yet", i)
		}
	}

	// Feed last — should return reassembled data
	result, err := reasm.Feed(frags[len(frags)-1])
	if err != nil {
		t.Fatalf("Feed last frag: %v", err)
	}
	if result == nil {
		t.Fatal("expected reassembled data")
	}

	// Verify reassembled data matches original frame bytes
	origBytes, _ := frame.EncodeBytes()
	if len(result) != len(origBytes) {
		t.Errorf("length: got %d, want %d", len(result), len(origBytes))
	}
	for i := range origBytes {
		if result[i] != origBytes[i] {
			t.Errorf("byte %d: got 0x%02X, want 0x%02X", i, result[i], origBytes[i])
			break
		}
	}
}

func TestFragmentReassemblyDuplicate(t *testing.T) {
	reasm := NewFragmentReassembler()

	frag := make([]byte, 20)
	frag[0] = 0x00
	frag[1] = 0x01 // FragSeq=1
	frag[2] = 0    // FragIdx=0
	frag[3] = 2    // FragTot=2
	// rest is payload

	// Feed same fragment twice
	reasm.Feed(frag)
	result, err := reasm.Feed(frag) // duplicate
	if err != nil {
		t.Fatal(err)
	}
	if result != nil {
		t.Error("duplicate fragment should not complete reassembly")
	}
}
