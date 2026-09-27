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
	HMACSize = 4 // truncated HMAC-SHA256 (§8.3)
	CRCSize  = 4 // CRC32 IEEE 802.3 (Appendix B)
)

// MaxTCPFrameSize is the largest frame a 2-byte length prefix can carry.
const MaxTCPFrameSize = 0xFFFF

// ComputeCRC32 computes the IEEE 802.3 CRC32 over the frame bytes (Appendix B).
// Input is the standard HID frame (header + payload, 12+N bytes).
func ComputeCRC32(frame []byte) [4]byte {
	sum := crc32.ChecksumIEEE(frame)
	var out [4]byte
	binary.BigEndian.PutUint32(out[:], sum)
	return out
}

// AppendTCPFrame appends a Length-Prefixed frame to dst for the DirectLink TCP
// mode (§7.2): 2-byte big-endian total frame length + the full HID frame.
// A single TCP stream may carry several such records back to back.
func AppendTCPFrame(dst []byte, frame *HIDFrame) ([]byte, error) {
	frameBytes, err := frame.EncodeBytes()
	if err != nil {
		return dst, err
	}
	if len(frameBytes) > MaxTCPFrameSize {
		return dst, fmt.Errorf("frame too large for TCP framing: %d bytes (max %d)", len(frameBytes), MaxTCPFrameSize)
	}
	dst = binary.BigEndian.AppendUint16(dst, uint16(len(frameBytes)))
	return append(dst, frameBytes...), nil
}

// ParseTCPFrames decodes every complete length-prefixed frame in buf (§7.2).
// It returns the frames and the number of bytes consumed; trailing bytes of an
// incomplete record are left for the caller to prepend to the next read.
func ParseTCPFrames(buf []byte) (frames []*HIDFrame, consumed int, err error) {
	for {
		rem := buf[consumed:]
		if len(rem) < 2 {
			return frames, consumed, nil
		}
		length := int(binary.BigEndian.Uint16(rem[0:2]))
		if length < HeaderSize {
			return frames, consumed, errors.New("TCP frame length below header size")
		}
		if len(rem) < 2+length {
			return frames, consumed, nil // wait for more bytes
		}
		frame, _, err := DecodeFrameBytes(rem[2 : 2+length])
		if err != nil {
			return frames, consumed, err
		}
		frames = append(frames, frame)
		consumed += 2 + length
	}
}

// MarshalEthernetFrame builds one Raw Ethernet frame (§7.2):
//
//	dstMAC(6) srcMAC(6) EtherType(2) frame(12+N) [CRC(4)] [HMAC(4)]
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
//	dstMAC(6) srcMAC(6) EtherType(2) frame(12+N) [CRC(4)] [HMAC(4)]
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
