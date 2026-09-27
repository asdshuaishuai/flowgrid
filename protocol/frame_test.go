package protocol

import (
	"bytes"
	"testing"
)

func TestFrameEncodeDecodeRoundTrip(t *testing.T) {
	tests := []struct {
		name  string
		frame HIDFrame
	}{
		{
			name: "heartbeat (no payload)",
			frame: HIDFrame{
				Header: FrameHeader{
					FrameType: FrameHeartbeat,
					Sequence:  0,
					Timestamp: 1234567890,
				},
				Payload: nil,
			},
		},
		{
			name: "key down",
			frame: HIDFrame{
				Header: FrameHeader{
					FrameType: FrameKeyDown,
					Sequence:  42,
					Timestamp: 9999999999,
				},
				Payload: (&KeyDownPayload{KeyCode: HIDKeyA, Modifiers: ModLeftCtrl}).Marshal(),
			},
		},
		{
			name: "mouse move",
			frame: HIDFrame{
				Header: FrameHeader{
					FrameType: FrameMouseMove,
					Sequence:  100,
					Timestamp: 1000000,
				},
				Payload: (&MouseMovePayload{DeltaX: -10, DeltaY: 25}).Marshal(),
			},
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			// Encode
			encoded, err := tt.frame.EncodeBytes()
			if err != nil {
				t.Fatalf("EncodeBytes: %v", err)
			}

			// Decode
			decoded, n, err := DecodeFrameBytes(encoded)
			if err != nil {
				t.Fatalf("DecodeFrameBytes: %v", err)
			}
			if n != len(encoded) {
				t.Errorf("consumed %d bytes, expected %d", n, len(encoded))
			}

			// Compare header
			if decoded.Header.FrameType != tt.frame.Header.FrameType {
				t.Errorf("FrameType: got 0x%02X, want 0x%02X", decoded.Header.FrameType, tt.frame.Header.FrameType)
			}
			if decoded.Header.Sequence != tt.frame.Header.Sequence {
				t.Errorf("Sequence: got %d, want %d", decoded.Header.Sequence, tt.frame.Header.Sequence)
			}
			if decoded.Header.Timestamp != tt.frame.Header.Timestamp {
				t.Errorf("Timestamp: got %d, want %d", decoded.Header.Timestamp, tt.frame.Header.Timestamp)
			}

			// Compare payload
			if !bytes.Equal(decoded.Payload, tt.frame.Payload) {
				t.Errorf("Payload: got %x, want %x", decoded.Payload, tt.frame.Payload)
			}
		})
	}
}

func TestFrameEncodeWriter(t *testing.T) {
	frame := HIDFrame{
		Header: FrameHeader{
			FrameType: FrameKeyDown,
			Sequence:  1,
			Timestamp: 100,
		},
		Payload: []byte{0x00, 0x04, 0x01}, // KeyA + Ctrl
	}

	var buf bytes.Buffer
	if err := frame.Encode(&buf); err != nil {
		t.Fatalf("Encode: %v", err)
	}

	decoded, err := DecodeFrame(&buf)
	if err != nil {
		t.Fatalf("DecodeFrame: %v", err)
	}

	if decoded.Header.FrameType != FrameKeyDown {
		t.Errorf("FrameType: got 0x%02X, want 0x%02X", decoded.Header.FrameType, FrameKeyDown)
	}
	if !bytes.Equal(decoded.Payload, frame.Payload) {
		t.Errorf("Payload mismatch")
	}
}

func TestFramePayloadLenMismatch(t *testing.T) {
	frame := HIDFrame{
		Header:  FrameHeader{PayloadLen: 5},
		Payload: []byte{1, 2, 3}, // len=3, header says 5
	}
	if err := frame.Validate(); err == nil {
		t.Error("expected validation error for length mismatch")
	}
}

func TestFrameBigEndianEncoding(t *testing.T) {
	frame := HIDFrame{
		Header: FrameHeader{
			FrameType: 0x42,
			Sequence:  0x0102, // should be encoded as [0x01, 0x02]
			Timestamp: 0x0102030405060708,
		},
		Payload: nil,
	}

	encoded, err := frame.EncodeBytes()
	if err != nil {
		t.Fatal(err)
	}

	// Check big-endian encoding
	if encoded[0] != 0x42 {
		t.Errorf("byte 0: got 0x%02X, want 0x42", encoded[0])
	}
	if encoded[1] != 0x01 || encoded[2] != 0x02 {
		t.Errorf("bytes 1-2: got %x, want [0x01, 0x02]", encoded[1:3])
	}
	// Timestamp at bytes 3-10
	if encoded[3] != 0x01 || encoded[10] != 0x08 {
		t.Errorf("timestamp bytes: got %x", encoded[3:11])
	}
}

func TestFrameHMACRoundTrip(t *testing.T) {
	key := []byte("0123456789abcdef0123456789abcdef")
	frame := HIDFrame{
		Header: FrameHeader{
			FrameType: FrameKeyDown,
			Sequence:  7,
			Timestamp: 42,
		},
		Payload: (&KeyDownPayload{KeyCode: HIDKeyC, Modifiers: ModLeftCtrl}).Marshal(),
	}

	encoded, err := frame.EncodeBytesWithHMAC(key)
	if err != nil {
		t.Fatalf("EncodeBytesWithHMAC: %v", err)
	}
	if len(encoded) != HeaderSize+len(frame.Payload)+HMACSize {
		t.Fatalf("encoded length %d, want header+payload+%d", len(encoded), HMACSize)
	}

	decoded, n, err := DecodeFrameBytesWithHMAC(encoded, key)
	if err != nil {
		t.Fatalf("DecodeFrameBytesWithHMAC: %v", err)
	}
	if n != len(encoded) {
		t.Errorf("consumed %d, want %d", n, len(encoded))
	}
	if !bytes.Equal(decoded.Payload, frame.Payload) {
		t.Errorf("payload mismatch")
	}
}

func TestFrameHMACTamperDetected(t *testing.T) {
	key := []byte("0123456789abcdef0123456789abcdef")
	frame := HIDFrame{
		Header:  FrameHeader{FrameType: FrameKeyDown, Sequence: 1, Timestamp: 1},
		Payload: []byte{0x00, 0x06, 0x01},
	}

	encoded, _ := frame.EncodeBytesWithHMAC(key)
	encoded[len(encoded)-1] ^= 0xFF // flip a bit in the HMAC itself
	if _, _, err := DecodeFrameBytesWithHMAC(encoded, key); err == nil {
		t.Error("expected HMAC mismatch for tampered MAC")
	}

	encoded, _ = frame.EncodeBytesWithHMAC(key)
	encoded[13] ^= 0xFF // flip a bit in the payload
	if _, _, err := DecodeFrameBytesWithHMAC(encoded, key); err == nil {
		t.Error("expected HMAC mismatch for tampered payload")
	}
}

func TestFrameHMACMissingTrailerRejected(t *testing.T) {
	// A key implies integrity is enabled: frames without the 4-byte HMAC
	// suffix must be rejected, not silently accepted (§8.3).
	key := []byte("0123456789abcdef0123456789abcdef")
	frame := HIDFrame{
		Header:  FrameHeader{FrameType: FrameHeartbeat, Sequence: 1, Timestamp: 1},
		Payload: nil,
	}

	plain, _ := frame.EncodeBytes()
	_, _, err := DecodeFrameBytesWithHMAC(plain, key)
	if err == nil {
		t.Fatal("expected error for missing HMAC trailer")
	}
	if pe, ok := err.(*ProtocolError); !ok || pe.Code != ErrHmacMismatch {
		t.Errorf("got %v, want ErrHmacMismatch", err)
	}

	// Without a key the plain frame decodes fine.
	if _, _, err := DecodeFrameBytesWithHMAC(plain, nil); err != nil {
		t.Errorf("plain decode without key: %v", err)
	}
}
