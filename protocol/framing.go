package protocol

import (
	"crypto/hmac"
	"encoding/binary"
	"errors"
	"fmt"
	"hash/crc32"
	"io"
)

// DirectLink transport framing (§7.2) and optional frame integrity (§8.3, Appendix B).

// EtherTypeFlowGrid is the custom EtherType for Raw Ethernet mode (§7.2).
const EtherTypeFlowGrid uint16 = 0xF1A0

// Integrity field sizes.
const (
	HMACSize = 8 // truncated HMAC-SHA256 (§8.3; 4-byte v1.0 truncation removed)
	CRCSize  = 4 // CRC32 IEEE 802.3 (Appendix B)
)

// maxTCPRecord is the largest sane length-prefixed TCP record: a max-size
// frame plus its HMAC trailer. Records claiming more than this are malformed.
const maxTCPRecord = HeaderSize + int(MaxPayloadLen) + HMACSize

// ComputeCRC32 computes the IEEE 802.3 CRC32 over the frame bytes (Appendix B).
// Input is the standard HID frame (header + payload, 12+N bytes).
func ComputeCRC32(frame []byte) [4]byte {
	sum := crc32.ChecksumIEEE(frame)
	var out [4]byte
	binary.BigEndian.PutUint32(out[:], sum)
	return out
}

// AppendTCPFrame appends a Length-Prefixed frame to dst for the DirectLink TCP
// mode (§7.2): 2-byte big-endian total record length + the full HID frame,
// optionally followed by the 8-byte frame HMAC (§8.3, DirectLink 建议启用).
// A single TCP stream may carry several such records back to back.
func AppendTCPFrame(dst []byte, frame *HIDFrame, hmacKey []byte) ([]byte, error) {
	frameBytes, err := frame.EncodeBytes()
	if err != nil {
		return dst, err
	}
	if len(hmacKey) > 0 {
		hm := ComputeHMAC(hmacKey, frameBytes)
		frameBytes = append(frameBytes, hm[:]...)
	}
	dst = binary.BigEndian.AppendUint16(dst, uint16(len(frameBytes)))
	return append(dst, frameBytes...), nil
}

// ParseTCPFrames decodes every complete length-prefixed frame in buf (§7.2).
// A non-empty hmacKey requires each record to carry the 8-byte HMAC trailer
// inside its length prefix; records without a valid MAC are dropped.
// It returns the frames and the number of bytes consumed; trailing bytes of an
// incomplete record are left for the caller to prepend to the next read.
// On a malformed record the error is returned together with a consumed count
// that skips past the bad record, so the caller can resume on the next one
// instead of retrying the same bytes forever.
func ParseTCPFrames(buf []byte, hmacKey []byte) (frames []*HIDFrame, consumed int, err error) {
	for {
		rem := buf[consumed:]
		if len(rem) < 2 {
			return frames, consumed, nil
		}
		length := int(binary.BigEndian.Uint16(rem[0:2]))
		if length < HeaderSize || length > maxTCPRecord {
			consumed += 2 // skip the bad prefix so the stream can resync
			return frames, consumed, errors.New("TCP frame length out of range")
		}
		if len(rem) < 2+length {
			return frames, consumed, nil // wait for more bytes
		}
		frame, recordConsumed, err := DecodeFrameBytesWithHMAC(rem[2:2+length], hmacKey)
		if err != nil {
			consumed += 2 + length // drop the whole malformed record
			return frames, consumed, err
		}
		frames = append(frames, frame)
		consumed += 2 + recordConsumed
	}
}

// MarshalEthernetFrame builds one Raw Ethernet frame (§7.2):
//
//	dstMAC(6) srcMAC(6) EtherType(2) frame(12+N) [CRC(4)] [HMAC(8)]
//
// CRC and HMAC, when enabled, are each computed over the standard HID frame
// bytes only (never over each other).
func MarshalEthernetFrame(dstMAC, srcMAC []byte, frame *HIDFrame, hmacKey []byte, withCRC bool) ([]byte, error) {
	frameBytes, err := frame.EncodeBytes()
	if err != nil {
		return nil, err
	}
	if len(dstMAC) != 6 || len(srcMAC) != 6 {
		return nil, errors.New("MAC addresses must be 6 bytes")
	}

	out := make([]byte, 0, 14+len(frameBytes)+CRCSize+HMACSize)
	out = append(out, dstMAC...)
	out = append(out, srcMAC...)
	out = binary.BigEndian.AppendUint16(out, EtherTypeFlowGrid)
	out = append(out, frameBytes...)
	if withCRC {
		crc := ComputeCRC32(frameBytes)
		out = append(out, crc[:]...)
	}
	if len(hmacKey) > 0 {
		hm := ComputeHMAC(hmacKey, frameBytes)
		out = append(out, hm[:]...)
	}
	return out, nil
}

// ParseEthernetFrame validates and strips the header and integrity trailer
// from a full Raw Ethernet frame as produced by MarshalEthernetFrame:
//
//	dstMAC(6) srcMAC(6) EtherType(2) frame(12+N) [CRC(4)] [HMAC(8)]
//
// It returns the standard HID frame bytes. Integrity options must match the
// ones used when marshalling.
func ParseEthernetFrame(data, hmacKey []byte, withCRC bool) ([]byte, error) {
	if len(data) < 14 {
		return nil, errors.New("ethernet frame too short")
	}
	etherType := binary.BigEndian.Uint16(data[12:14])
	if etherType != EtherTypeFlowGrid {
		return nil, fmt.Errorf("unexpected EtherType: 0x%04X", etherType)
	}
	rest := data[14:]
	if len(rest) < HeaderSize {
		return nil, io.ErrUnexpectedEOF
	}
	total := HeaderSize + int(rest[11]) // header + payload length
	if len(rest) < total {
		return nil, io.ErrUnexpectedEOF
	}
	frameBytes := rest[:total]
	tail := rest[total:]

	if withCRC {
		if len(tail) < CRCSize {
			return nil, errors.New("missing CRC")
		}
		crc := ComputeCRC32(frameBytes)
		if !hmac.Equal(crc[:], tail[:CRCSize]) {
			return nil, NewError(ErrHmacMismatch, "CRC mismatch")
		}
		tail = tail[CRCSize:]
	}
	if len(hmacKey) > 0 {
		if len(tail) < HMACSize {
			return nil, NewError(ErrHmacMismatch, "missing HMAC")
		}
		expected := ComputeHMAC(hmacKey, frameBytes)
		if !hmac.Equal(expected[:], tail[:HMACSize]) {
			return nil, NewError(ErrHmacMismatch, "HMAC mismatch")
		}
	}
	return frameBytes, nil
}
