package protocol

import (
	"bytes"
	"testing"
)

func testFrame(seq uint16, payloadLen int) *HIDFrame {
	payload := make([]byte, payloadLen)
	for i := range payload {
		payload[i] = byte(i)
	}
	return &HIDFrame{
		Header:  FrameHeader{FrameType: FrameClipboard, Sequence: seq, Timestamp: 1000},
		Payload: payload,
	}
}

func TestAppendTCPRoundTrip(t *testing.T) {
	frames := []*HIDFrame{testFrame(1, 0), testFrame(2, 3), testFrame(3, 255)}

	var buf []byte
	for _, f := range frames {
		var err error
		buf, err = AppendTCPFrame(buf, f, nil)
		if err != nil {
			t.Fatalf("AppendTCPFrame: %v", err)
		}
	}

	decoded, consumed, err := ParseTCPFrames(buf, nil)
	if err != nil {
		t.Fatalf("ParseTCPFrames: %v", err)
	}
	if consumed != len(buf) {
		t.Errorf("consumed %d, want %d", consumed, len(buf))
	}
	if len(decoded) != len(frames) {
		t.Fatalf("decoded %d frames, want %d", len(decoded), len(frames))
	}
	for i, f := range frames {
		if decoded[i].Header.Sequence != f.Header.Sequence {
			t.Errorf("frame %d sequence: got %d, want %d", i, decoded[i].Header.Sequence, f.Header.Sequence)
		}
		if !bytes.Equal(decoded[i].Payload, f.Payload) {
			t.Errorf("frame %d payload mismatch", i)
		}
	}
}

func TestParseTCPPartialStream(t *testing.T) {
	// A TCP read may split records anywhere; the parser must only consume
	// complete records and report the rest as pending.
	f1, f2 := testFrame(1, 4), testFrame(2, 4)
	buf, err := AppendTCPFrame(nil, f1, nil)
	if err != nil {
		t.Fatal(err)
	}
	buf, err = AppendTCPFrame(buf, f2, nil)
	if err != nil {
		t.Fatal(err)
	}

	frames, consumed, err := ParseTCPFrames(buf[:len(buf)-3], nil) // cut into frame 2
	if err != nil {
		t.Fatalf("ParseTCPFrames: %v", err)
	}
	if len(frames) != 1 || frames[0].Header.Sequence != 1 {
		t.Fatalf("expected only frame 1, got %d frames", len(frames))
	}

	frames, _, err = ParseTCPFrames(append(buf[consumed:len(buf)-3], buf[len(buf)-3:]...), nil)
	if err != nil {
		t.Fatalf("ParseTCPFrames rest: %v", err)
	}
	if len(frames) != 1 || frames[0].Header.Sequence != 2 {
		t.Fatalf("expected frame 2 after reassembly, got %d frames", len(frames))
	}
}

func TestParseTCPInvalidLength(t *testing.T) {
	buf := []byte{0x00, 0x05, 0x01, 0x02, 0x03, 0x04, 0x05} // length 5 < HeaderSize
	frames, consumed, err := ParseTCPFrames(buf, nil)
	if err == nil {
		t.Error("expected error for length below header size")
	}
	if consumed != 2 {
		t.Errorf("consumed %d, want 2 (bad prefix skipped for resync)", consumed)
	}
	if len(frames) != 0 {
		t.Errorf("decoded %d frames, want 0", len(frames))
	}
}

// A record that parses as a length but whose content is malformed must be
// skipped entirely (error + consumed past it) so the caller does not retry the
// same bytes forever.
func TestParseTCPMalformedRecordResyncs(t *testing.T) {
	// Record 1: length 12 (header only) but its PayloadLen byte claims 255
	// bytes — a truncated frame. Record 2 appended right behind it is healthy.
	bad := []byte{0x00, 0x0C}
	bad = append(bad, make([]byte, 12)...)
	bad[13] = 0xFF // record 1 content[11] = PayloadLen 255, but the record ends
	buf, err := AppendTCPFrame(bad, testFrame(7, 2), nil)
	if err != nil {
		t.Fatal(err)
	}

	frames, consumed, err := ParseTCPFrames(buf, nil)
	if err == nil {
		t.Fatal("expected error for malformed record")
	}
	if len(frames) != 0 {
		t.Fatalf("expected no frames yet, got %d", len(frames))
	}
	// consumed must point at the start of record 2
	rest, _, err := ParseTCPFrames(buf[consumed:], nil)
	if err != nil {
		t.Fatalf("resume after resync: %v", err)
	}
	if len(rest) != 1 || rest[0].Header.Sequence != 7 {
		t.Fatalf("expected frame 7 after resync, got %d frames", len(rest))
	}
}

// TCP frames must be HMAC-verified when a key is configured (§8.3): the HMAC
// trailer lives inside the length prefix, and a tampered or missing MAC
// rejects the whole record.
func TestParseTCPHMACRoundTrip(t *testing.T) {
	key := []byte("tcp-session-key-0123456789abcdef")
	buf, err := AppendTCPFrame(nil, testFrame(1, 6), key)
	if err != nil {
		t.Fatal(err)
	}
	buf, err = AppendTCPFrame(buf, testFrame(2, 6), key)
	if err != nil {
		t.Fatal(err)
	}

	frames, consumed, err := ParseTCPFrames(buf, key)
	if err != nil {
		t.Fatalf("ParseTCPFrames: %v", err)
	}
	if consumed != len(buf) {
		t.Errorf("consumed %d, want %d", consumed, len(buf))
	}
	if len(frames) != 2 || frames[0].Header.Sequence != 1 || frames[1].Header.Sequence != 2 {
		t.Fatalf("expected frames 1 and 2, got %d", len(frames))
	}

	// Tamper with one byte inside record 1's payload: that record must be
	// rejected while record 2 still parses after the resync.
	bad := append([]byte(nil), buf...)
	bad[2+12] ^= 0xFF
	frames, consumed, err = ParseTCPFrames(bad, key)
	if err == nil {
		t.Fatal("expected HMAC mismatch error")
	}
	if len(frames) != 0 {
		t.Fatalf("tampered record must not decode, got %d", len(frames))
	}
	rest, _, err := ParseTCPFrames(bad[consumed:], key)
	if err != nil || len(rest) != 1 || rest[0].Header.Sequence != 2 {
		t.Fatalf("expected frame 2 after resync, got %d frames (err=%v)", len(rest), err)
	}
}

func TestEthernetFrameRoundTrip(t *testing.T) {
	dst := []byte{0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff}
	src := []byte{0x11, 0x22, 0x33, 0x44, 0x55, 0x66}
	key := []byte("session-key-bytes")
	frame := testFrame(9, 8)

	tests := []struct {
		name    string
		hmacKey []byte
		withCRC bool
	}{
		{"bare", nil, false},
		{"crc only", nil, true},
		{"hmac only", key, false},
		{"crc + hmac", key, true},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			wire, err := MarshalEthernetFrame(dst, src, frame, tt.hmacKey, tt.withCRC)
			if err != nil {
				t.Fatalf("MarshalEthernetFrame: %v", err)
			}

			wantLen := 14 + HeaderSize + len(frame.Payload)
			if tt.withCRC {
				wantLen += CRCSize
			}
			if tt.hmacKey != nil {
				wantLen += HMACSize
			}
			if len(wire) != wantLen {
				t.Fatalf("wire length %d, want %d", len(wire), wantLen)
			}
			// EtherType at bytes 12-13
			if wire[12] != 0xF1 || wire[13] != 0xA0 {
				t.Errorf("EtherType: got %x, want f1a0", wire[12:14])
			}

			got, err := ParseEthernetFrame(wire, tt.hmacKey, tt.withCRC)
			if err != nil {
				t.Fatalf("ParseEthernetFrame: %v", err)
			}
			plain, _ := frame.EncodeBytes()
			if !bytes.Equal(got, plain) {
				t.Errorf("frame bytes mismatch: got %x, want %x", got, plain)
			}
		})
	}
}

func TestEthernetFrameIntegrityFailure(t *testing.T) {
	dst := []byte{1, 2, 3, 4, 5, 6}
	src := []byte{6, 5, 4, 3, 2, 1}
	key := []byte("session-key-bytes")
	frame := testFrame(1, 4)

	wire, _ := MarshalEthernetFrame(dst, src, frame, key, true)

	// Corrupt the CRC byte
	bad := append([]byte(nil), wire...)
	bad[14+HeaderSize+4] ^= 0xFF
	if _, err := ParseEthernetFrame(bad, key, true); err == nil {
		t.Error("expected CRC mismatch")
	}

	// Corrupt the HMAC byte
	bad = append([]byte(nil), wire...)
	bad[len(bad)-1] ^= 0xFF
	if _, err := ParseEthernetFrame(bad, key, true); err == nil {
		t.Error("expected HMAC mismatch")
	}

	// Wrong EtherType
	bad = append([]byte(nil), wire...)
	bad[13] = 0x00
	if _, err := ParseEthernetFrame(bad, key, true); err == nil {
		t.Error("expected EtherType error")
	}
}

func TestMarshalEthernetFrameInvalidMAC(t *testing.T) {
	if _, err := MarshalEthernetFrame([]byte{1, 2, 3}, []byte{4, 5, 6, 7, 8, 9}, testFrame(1, 0), nil, false); err == nil {
		t.Error("expected error for short MAC")
	}
}

func TestComputeCRC32KnownValue(t *testing.T) {
	// CRC32-IEEE of "123456789" is 0xCBF43926 (canonical check value).
	got := ComputeCRC32([]byte("123456789"))
	want := []byte{0xCB, 0xF4, 0x39, 0x26}
	if !bytes.Equal(got[:], want) {
		t.Errorf("CRC32: got %x, want %x", got, want)
	}
}
