package protocol

import (
	"bytes"
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
	frags, err := FragmentFrame(frame, DefaultMTU)
	if err != nil {
		t.Fatal(err)
	}
	if frags != nil {
		t.Error("small frame should not be fragmented")
	}
}

func TestFragmentFrameSplit(t *testing.T) {
	// Legal-size frame (12-byte header + 3-byte KEY_DOWN payload) split by a
	// tiny MTU. Frames with payloads over 255 bytes cannot be encoded at all
	// (§5.1 UInt8 length), so fragmentation is exercised on a small frame.
	frame := &HIDFrame{
		Header: FrameHeader{
			FrameType: FrameKeyDown,
			Sequence:  42,
			Timestamp: 999,
		},
		Payload: []byte{0x00, 0x04, 0x01},
	}

	frags, err := FragmentFrame(frame, 12) // maxPayload = 12-4 = 8 < 15 frame bytes
	if err != nil {
		t.Fatal(err)
	}
	if frags == nil {
		t.Fatal("frame larger than maxPayload should be fragmented")
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

	if _, err := FragmentFrame(frame, FragHeaderSize); err == nil {
		t.Error("mtu <= FragHeaderSize should be rejected")
	}
}

func TestFragmentReassembly(t *testing.T) {
	reasm := NewFragmentReassembler()

	frame := &HIDFrame{
		Header: FrameHeader{
			FrameType: FrameClipboard,
			Sequence:  7,
			Timestamp: 12345,
		},
		Payload: bytes.Repeat([]byte{0xAB}, 20),
	}

	frags, err := FragmentFrame(frame, 20) // maxPayload = 16 < 32 frame bytes
	if err != nil {
		t.Fatal(err)
	}
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

// Regression: a fragment whose own FragTot passes validation but disagrees
// with the group's total must be rejected, not index the group out of range
// (previously a remote panic).
func TestFragmentTotMismatchRejected(t *testing.T) {
	reasm := NewFragmentReassembler()

	first := append((&FragHeader{FragSeq: 42, FragIdx: 0, FragTot: 2}).Marshal(), []byte("AAAA")...)
	if _, err := reasm.Feed(first); err != nil {
		t.Fatalf("first feed: %v", err)
	}

	attacker := append((&FragHeader{FragSeq: 42, FragIdx: 200, FragTot: 255}).Marshal(), []byte("BBBB")...)
	out, err := reasm.Feed(attacker)
	if err == nil {
		t.Fatal("FragTot mismatch must be rejected")
	}
	if out != nil {
		t.Error("no data should be assembled from a rejected fragment")
	}
}

// Regression: the reassembler must copy fragment bytes, because cgo callers
// free their buffer right after Feed returns.
func TestFeedCopiesCallerBuffer(t *testing.T) {
	reasm := NewFragmentReassembler()

	frag := make([]byte, 8)
	frag[0] = 0x00
	frag[1] = 0x01 // FragSeq=1
	frag[2] = 0    // FragIdx=0
	frag[3] = 2    // FragTot=2
	copy(frag[4:], []byte("AAAA"))

	if _, err := reasm.Feed(frag); err != nil {
		t.Fatal(err)
	}

	// Clobber the caller's buffer after Feed — the stored fragment must not
	// alias it.
	copy(frag[4:], []byte("XXXX"))

	last := append((&FragHeader{FragSeq: 1, FragIdx: 1, FragTot: 2}).Marshal(), []byte("BBBB")...)
	out, err := reasm.Feed(last)
	if err != nil {
		t.Fatal(err)
	}
	if out == nil {
		t.Fatal("expected reassembled data")
	}
	// Assembled = fragment payloads concatenated: [0:4]="AAAA" (frag 0), [4:8]="BBBB".
	if string(out[0:4]) != "AAAA" {
		t.Errorf("reassembled data follows the mutated buffer: %q", out[0:4])
	}
	if string(out[4:8]) != "BBBB" {
		t.Errorf("unexpected second fragment content: %q", out[4:8])
	}
}

// The reassembler must cap concurrent groups so a flood of never-completing
// fragments cannot grow memory without bound.
func TestFragmentGroupCap(t *testing.T) {
	reasm := NewFragmentReassembler()

	for seq := 0; seq < maxConcurrentGroups+10; seq++ {
		frag := append((&FragHeader{FragSeq: uint16(seq), FragIdx: 0, FragTot: 2}).Marshal(), []byte("AAAA")...)
		if _, err := reasm.Feed(frag); err != nil {
			t.Fatalf("frag seq %d: %v", seq, err)
		}
	}
	if len(reasm.groups) > maxConcurrentGroups {
		t.Errorf("group count %d exceeds cap %d", len(reasm.groups), maxConcurrentGroups)
	}
}
