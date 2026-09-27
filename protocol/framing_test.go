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
		buf, err = AppendTCPFrame(buf, f)
		if err != nil {
			t.Fatalf("AppendTCPFrame: %v", err)
		}
	}

	decoded, consumed, err := ParseTCPFrames(buf)
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
	buf, err := AppendTCPFrame(nil, f1)
	if err != nil {
		t.Fatal(err)
	}
	buf, err = AppendTCPFrame(buf, f2)
	if err != nil {
		t.Fatal(err)
	}

	frames, consumed, err := ParseTCPFrames(buf[:len(buf)-3]) // cut into frame 2
	if err != nil {
		t.Fatalf("ParseTCPFrames: %v", err)
	}
	if len(frames) != 1 || frames[0].Header.Sequence != 1 {
		t.Fatalf("expected only frame 1, got %d frames", len(frames))
	}

	frames, _, err = ParseTCPFrames(append(buf[consumed:len(buf)-3], buf[len(buf)-3:]...))
	if err != nil {
		t.Fatalf("ParseTCPFrames rest: %v", err)
	}
	if len(frames) != 1 || frames[0].Header.Sequence != 2 {
		t.Fatalf("expected frame 2 after reassembly, got %d frames", len(frames))
	}
}

func TestParseTCPInvalidLength(t *testing.T) {
	buf := []byte{0x00, 0x05, 0x01, 0x02, 0x03, 0x04, 0x05} // length 5 < HeaderSize
	if _, _, err := ParseTCPFrames(buf); err == nil {
		t.Error("expected error for length below header size")
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
