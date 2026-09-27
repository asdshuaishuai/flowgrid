package protocol

import (
	"crypto/hmac"
	"crypto/sha256"
	"encoding/binary"
	"errors"
	"io"
)

// Frame type constants (§5.2)
const (
	FrameKeyDown     uint8 = 0x01
	FrameKeyUp       uint8 = 0x02
	FrameMouseMove   uint8 = 0x03
	FrameMouseButton uint8 = 0x04
	FrameScroll      uint8 = 0x05
	FrameGesture     uint8 = 0x06 // reserved
	FrameClipboard   uint8 = 0x07
	FrameHeartbeat   uint8 = 0x08
	FrameLatencyPing uint8 = 0x09
	FrameLatencyPong uint8 = 0x0A
	FrameIdentify    uint8 = 0x10
	FrameIdentifyRes uint8 = 0x11
	FrameDisconnect  uint8 = 0x20
	FrameError       uint8 = 0xFF
)

// Protocol version constants (§2.3)
const (
	ProtocolVersion uint8  = 1
	FrameVersion    uint8  = 1
	KeyMapVersion          = "1.0"
	MaxPayloadLen   uint8  = 255
	HeaderSize      int    = 12
	DefaultUDPPort  uint16 = 24801
)

// FrameHeader is the 12-byte header shared by all HID frames (§5.1).
//
//	Offset 0:  FrameType   (1 byte)
//	Offset 1:  Sequence    (2 bytes, UInt16 BE)
//	Offset 3:  Timestamp   (8 bytes, UInt64 BE, microseconds)
//	Offset 11: PayloadLen  (1 byte)
type FrameHeader struct {
	FrameType  uint8
	Sequence   uint16
	Timestamp  uint64 // microseconds since epoch
	PayloadLen uint8
}

// HIDFrame is a complete protocol frame: header + payload (§5.1).
type HIDFrame struct {
	Header  FrameHeader
	Payload []byte // length must match Header.PayloadLen
}

// Encode writes the frame to w in wire format (big-endian, tight packing).
func (f *HIDFrame) Encode(w io.Writer) error {
	f.Header.PayloadLen = uint8(len(f.Payload))

	var buf [HeaderSize]byte
	buf[0] = f.Header.FrameType
	binary.BigEndian.PutUint16(buf[1:3], f.Header.Sequence)
	binary.BigEndian.PutUint64(buf[3:11], f.Header.Timestamp)
	buf[11] = f.Header.PayloadLen

	if _, err := w.Write(buf[:]); err != nil {
		return err
	}
	if len(f.Payload) > 0 {
		if _, err := w.Write(f.Payload); err != nil {
			return err
		}
	}
	return nil
}

// EncodeBytes returns the frame as a byte slice.
func (f *HIDFrame) EncodeBytes() ([]byte, error) {
	var buf []byte
	f.Header.PayloadLen = uint8(len(f.Payload))

	hdr := make([]byte, HeaderSize)
	hdr[0] = f.Header.FrameType
	binary.BigEndian.PutUint16(hdr[1:3], f.Header.Sequence)
	binary.BigEndian.PutUint64(hdr[3:11], f.Header.Timestamp)
	hdr[11] = f.Header.PayloadLen

	buf = append(hdr, f.Payload...)
	return buf, nil
}

// EncodeBytesWithHMAC returns the frame with a 4-byte HMAC-SHA256 truncation appended.
// HMAC is computed over header+payload using the provided key.
func (f *HIDFrame) EncodeBytesWithHMAC(key []byte) ([]byte, error) {
	var buf []byte
	f.Header.PayloadLen = uint8(len(f.Payload))

	hdr := make([]byte, HeaderSize)
	hdr[0] = f.Header.FrameType
	binary.BigEndian.PutUint16(hdr[1:3], f.Header.Sequence)
	binary.BigEndian.PutUint64(hdr[3:11], f.Header.Timestamp)
	hdr[11] = f.Header.PayloadLen

	buf = append(hdr, f.Payload...)
	if len(key) > 0 {
		hmac := ComputeHMAC(key, buf)
		buf = append(buf, hmac[:]...)
	}
	return buf, nil
}

// ComputeHMAC computes SHA-256(key, data) and returns the first 4 bytes.
func ComputeHMAC(key, data []byte) [4]byte {
	mac := hmac.New(sha256.New, key)
	mac.Write(data)
	sum := mac.Sum(nil)
	var out [4]byte
	copy(out[:], sum[:4])
	return out
}

// DecodeFrameBytesWithHMAC decodes a frame and verifies the trailing 4-byte HMAC (§8.3).
// When key is non-empty the frame MUST carry the 4-byte HMAC suffix; a missing or
// mismatching HMAC is an ErrHmacMismatch error — frames are never accepted unverified.
// Returns the frame and total bytes consumed (including the HMAC).
func DecodeFrameBytesWithHMAC(data, key []byte) (*HIDFrame, int, error) {
	frame, total, err := DecodeFrameBytes(data)
	if err != nil {
		return nil, 0, err
	}

	if len(key) > 0 {
		if len(data) < total+4 {
			return nil, 0, NewError(ErrHmacMismatch, "missing HMAC")
		}
		gotHMAC := data[total : total+4]
		expected := ComputeHMAC(key, data[:total])
		if !hmac.Equal(gotHMAC, expected[:]) {
			return nil, 0, NewError(ErrHmacMismatch, "HMAC mismatch")
		}
		return frame, total + 4, nil
	}

	return frame, total, nil
}

// DecodeFrame reads one HIDFrame from r.
func DecodeFrame(r io.Reader) (*HIDFrame, error) {
	var hdrBuf [HeaderSize]byte
	if _, err := io.ReadFull(r, hdrBuf[:]); err != nil {
		return nil, err
	}

	hdr := FrameHeader{
		FrameType:  hdrBuf[0],
		Sequence:   binary.BigEndian.Uint16(hdrBuf[1:3]),
		Timestamp:  binary.BigEndian.Uint64(hdrBuf[3:11]),
		PayloadLen: hdrBuf[11],
	}

	var payload []byte
	if hdr.PayloadLen > 0 {
		payload = make([]byte, hdr.PayloadLen)
		if _, err := io.ReadFull(r, payload); err != nil {
			return nil, err
		}
	}

	return &HIDFrame{Header: hdr, Payload: payload}, nil
}

// DecodeFrameBytes decodes a HIDFrame from a byte slice.
// Returns the frame and number of bytes consumed.
func DecodeFrameBytes(data []byte) (*HIDFrame, int, error) {
	if len(data) < HeaderSize {
		return nil, 0, io.ErrUnexpectedEOF
	}

	hdr := FrameHeader{
		FrameType:  data[0],
		Sequence:   binary.BigEndian.Uint16(data[1:3]),
		Timestamp:  binary.BigEndian.Uint64(data[3:11]),
		PayloadLen: data[11],
	}

	total := HeaderSize + int(hdr.PayloadLen)
	if len(data) < total {
		return nil, 0, io.ErrUnexpectedEOF
	}

	var payload []byte
	if hdr.PayloadLen > 0 {
		payload = make([]byte, hdr.PayloadLen)
		copy(payload, data[HeaderSize:total])
	}

	return &HIDFrame{Header: hdr, Payload: payload}, total, nil
}

// Validate checks that the frame header is consistent.
func (f *HIDFrame) Validate() error {
	if len(f.Payload) != int(f.Header.PayloadLen) {
		return errors.New("payload length mismatch")
	}
	if int(f.Header.PayloadLen) > int(MaxPayloadLen) {
		return errors.New("payload exceeds max length")
	}
	return nil
}
