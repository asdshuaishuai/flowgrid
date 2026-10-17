package protocol

import (
	"crypto/hmac"
	"crypto/sha256"
	"encoding/binary"
	"errors"
	"fmt"
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

// File transfer frame types (§7.4). Per Appendix D, frame types added after
// v1.0 use values > 0x20.
const (
	FrameFileOffer uint8 = 0x21 // Sender → Receiver: 发起传输，等对端确认
	FrameFileAck   uint8 = 0x22 // Receiver → Sender: 接受/拒绝/完成确认
	FrameFileChunk uint8 = 0x23 // Sender → Receiver: 文件数据块
	FrameFileAbort uint8 = 0x24 // 双向: 取消传输
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

// checkPayloadLen rejects payloads that cannot be represented in the 1-byte
// wire length field (§5.1). Encoding an oversized payload would silently wrap
// the length byte and corrupt the frame, so it must fail loudly instead.
func (f *HIDFrame) checkPayloadLen() error {
	if len(f.Payload) > int(MaxPayloadLen) {
		return NewError(ErrInvalidPayload, fmt.Sprintf("payload too large: %d bytes (max %d)", len(f.Payload), MaxPayloadLen))
	}
	return nil
}

// headerBytes renders the 12-byte header (§5.1).
//
// 三个编码入口（Encode / EncodeBytes / EncodeBytesWithHMAC）原先各自抄了一遍
// 这段字节布局，改一处漏两处就会发出对不上的帧——布局只留这一个来源。
// PayloadLen 以 Payload 长度为准，顺手把 Header 里的值也补齐，方便调用方
// 编码后直接读 Header。
func (f *HIDFrame) headerBytes() [HeaderSize]byte {
	f.Header.PayloadLen = uint8(len(f.Payload))

	var buf [HeaderSize]byte
	buf[0] = f.Header.FrameType
	binary.BigEndian.PutUint16(buf[1:3], f.Header.Sequence)
	binary.BigEndian.PutUint64(buf[3:11], f.Header.Timestamp)
	buf[11] = f.Header.PayloadLen
	return buf
}

// Encode writes the frame to w in wire format (big-endian, tight packing).
func (f *HIDFrame) Encode(w io.Writer) error {
	if err := f.checkPayloadLen(); err != nil {
		return err
	}
	hdr := f.headerBytes()

	if _, err := w.Write(hdr[:]); err != nil {
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
	if err := f.checkPayloadLen(); err != nil {
		return nil, err
	}
	hdr := f.headerBytes()
	return append(hdr[:], f.Payload...), nil
}

// EncodeBytesWithHMAC returns the frame with an 8-byte HMAC-SHA256 truncation appended.
// HMAC is computed over header+payload using the provided key.
func (f *HIDFrame) EncodeBytesWithHMAC(key []byte) ([]byte, error) {
	if err := f.checkPayloadLen(); err != nil {
		return nil, err
	}
	hdr := f.headerBytes()
	buf := append(hdr[:], f.Payload...)
	if len(key) > 0 {
		hmac := ComputeHMAC(key, buf)
		buf = append(buf, hmac[:]...)
	}
	return buf, nil
}

// ComputeHMAC computes HMAC-SHA256(key, data) and returns the first 8 bytes (§8.3).
// 64-bit truncation keeps online forgery at ~2^64 attempts; v1.0's 4-byte
// truncation was brute-forceable at line rate.
func ComputeHMAC(key, data []byte) [8]byte {
	mac := hmac.New(sha256.New, key)
	mac.Write(data)
	sum := mac.Sum(nil)
	var out [8]byte
	copy(out[:], sum[:8])
	return out
}

// DecodeFrameBytesWithHMAC decodes a frame and verifies the trailing 8-byte HMAC (§8.3).
// A non-empty key means integrity is REQUIRED: the frame must carry the 8-byte
// HMAC suffix, and a missing or mismatching MAC is an ErrHmacMismatch error.
// An empty key disables verification entirely — only pass an empty key on
// links where integrity is provided below this layer (DTLS/TLS).
// Returns the frame and total bytes consumed (including the HMAC).
func DecodeFrameBytesWithHMAC(data, key []byte) (*HIDFrame, int, error) {
	frame, total, err := DecodeFrameBytes(data)
	if err != nil {
		return nil, 0, err
	}

	if len(key) > 0 {
		if len(data) < total+HMACSize {
			return nil, 0, NewError(ErrHmacMismatch, "missing HMAC")
		}
		gotHMAC := data[total : total+HMACSize]
		expected := ComputeHMAC(key, data[:total])
		if !hmac.Equal(gotHMAC, expected[:]) {
			return nil, 0, NewError(ErrHmacMismatch, "HMAC mismatch")
		}
		return frame, total+HMACSize, nil
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

// Validate checks that the frame header is consistent. The 255-byte payload
// ceiling is enforced at encode time (checkPayloadLen) — PayloadLen is a
// uint8 and cannot exceed it by construction.
func (f *HIDFrame) Validate() error {
	if len(f.Payload) != int(f.Header.PayloadLen) {
		return errors.New("payload length mismatch")
	}
	return nil
}
