package protocol

import (
	"encoding/binary"
	"errors"
)

// --- Payload structs for each frame type (§5.3) ---

// KeyDownPayload is the payload for KEY_DOWN (0x01) — 3 bytes.
type KeyDownPayload struct {
	KeyCode   uint16 // HID Usage ID (§6.1)
	Modifiers uint8  // modifier bitmask (§6.2)
}

func (p *KeyDownPayload) Marshal() []byte {
	buf := make([]byte, 3)
	binary.BigEndian.PutUint16(buf[0:2], p.KeyCode)
	buf[2] = p.Modifiers
	return buf
}

func UnmarshalKeyDown(data []byte) (*KeyDownPayload, error) {
	if len(data) < 3 {
		return nil, errors.New("KEY_DOWN payload too short")
	}
	return &KeyDownPayload{
		KeyCode:   binary.BigEndian.Uint16(data[0:2]),
		Modifiers: data[2],
	}, nil
}

// KeyUpPayload is the payload for KEY_UP (0x02) — 2 bytes.
type KeyUpPayload struct {
	KeyCode uint16 // HID Usage ID
}

func (p *KeyUpPayload) Marshal() []byte {
	buf := make([]byte, 2)
	binary.BigEndian.PutUint16(buf[0:2], p.KeyCode)
	return buf
}

func UnmarshalKeyUp(data []byte) (*KeyUpPayload, error) {
	if len(data) < 2 {
		return nil, errors.New("KEY_UP payload too short")
	}
	return &KeyUpPayload{
		KeyCode: binary.BigEndian.Uint16(data[0:2]),
	}, nil
}

// MouseMovePayload is the payload for MOUSE_MOVE (0x03) — 4 bytes.
type MouseMovePayload struct {
	DeltaX int16 // relative X offset
	DeltaY int16 // relative Y offset
}

func (p *MouseMovePayload) Marshal() []byte {
	buf := make([]byte, 4)
	binary.BigEndian.PutUint16(buf[0:2], uint16(p.DeltaX))
	binary.BigEndian.PutUint16(buf[2:4], uint16(p.DeltaY))
	return buf
}

func UnmarshalMouseMove(data []byte) (*MouseMovePayload, error) {
	if len(data) < 4 {
		return nil, errors.New("MOUSE_MOVE payload too short")
	}
	return &MouseMovePayload{
		DeltaX: int16(binary.BigEndian.Uint16(data[0:2])),
		DeltaY: int16(binary.BigEndian.Uint16(data[2:4])),
	}, nil
}

// Mouse button IDs (§5.3 MOUSE_BTN)
const (
	MouseButtonLeft   uint8 = 0x01
	MouseButtonRight  uint8 = 0x02
	MouseButtonMiddle uint8 = 0x03
	MouseButtonBack   uint8 = 0x04
	MouseButtonForward uint8 = 0x05
)

// MouseButtonPayload is the payload for MOUSE_BTN (0x04) — 2 bytes.
type MouseButtonPayload struct {
	ButtonID uint8 // button identifier
	State    uint8 // 0x00 = release, 0x01 = press
}

func (p *MouseButtonPayload) Marshal() []byte {
	return []byte{p.ButtonID, p.State}
}

func UnmarshalMouseButton(data []byte) (*MouseButtonPayload, error) {
	if len(data) < 2 {
		return nil, errors.New("MOUSE_BTN payload too short")
	}
	return &MouseButtonPayload{
		ButtonID: data[0],
		State:    data[1],
	}, nil
}

// ScrollPayload is the payload for SCROLL (0x05) — 4 bytes.
// Units are pixels (pixel-based scrolling).
type ScrollPayload struct {
	DeltaY int16 // vertical scroll (positive = up)
	DeltaX int16 // horizontal scroll (positive = left)
}

func (p *ScrollPayload) Marshal() []byte {
	buf := make([]byte, 4)
	binary.BigEndian.PutUint16(buf[0:2], uint16(p.DeltaY))
	binary.BigEndian.PutUint16(buf[2:4], uint16(p.DeltaX))
	return buf
}

func UnmarshalScroll(data []byte) (*ScrollPayload, error) {
	if len(data) < 4 {
		return nil, errors.New("SCROLL payload too short")
	}
	return &ScrollPayload{
		DeltaY: int16(binary.BigEndian.Uint16(data[0:2])),
		DeltaX: int16(binary.BigEndian.Uint16(data[2:4])),
	}, nil
}

// ClipboardPayload is the payload for CLIPBOARD (0x07) — variable length.
// Max data length: 65536 bytes (64KB). Larger payloads use fragmentation.
type ClipboardPayload struct {
	MIMEType string // e.g. "text/plain"
	Data     []byte
}

const MaxClipboardSize = 65536

func (p *ClipboardPayload) Marshal() []byte {
	mimeBytes := []byte(p.MIMEType)
	mimeLen := uint8(len(mimeBytes))
	dataLen := uint32(len(p.Data))

	buf := make([]byte, 1+int(mimeLen)+4+len(p.Data))
	buf[0] = mimeLen
	copy(buf[1:1+mimeLen], mimeBytes)
	binary.BigEndian.PutUint32(buf[1+mimeLen:5+mimeLen], dataLen)
	copy(buf[5+mimeLen:], p.Data)
	return buf
}

func UnmarshalClipboard(data []byte) (*ClipboardPayload, error) {
	if len(data) < 5 {
		return nil, errors.New("CLIPBOARD payload too short")
	}
	mimeLen := int(data[0])
	if len(data) < 1+mimeLen+4 {
		return nil, errors.New("CLIPBOARD payload truncated")
	}
	mimeType := string(data[1 : 1+mimeLen])
	dataLen := binary.BigEndian.Uint32(data[1+mimeLen : 5+mimeLen])
	if len(data) < 5+mimeLen+int(dataLen) {
		return nil, errors.New("CLIPBOARD data truncated")
	}
	clipData := make([]byte, dataLen)
	copy(clipData, data[5+mimeLen:5+mimeLen+int(dataLen)])
	return &ClipboardPayload{MIMEType: mimeType, Data: clipData}, nil
}

// LatencyPingPayload is the payload for LATENCY_PING (0x09) — 8 bytes.
type LatencyPingPayload struct {
	Timestamp uint64 // send time in microseconds
}

func (p *LatencyPingPayload) Marshal() []byte {
	buf := make([]byte, 8)
	binary.BigEndian.PutUint64(buf, p.Timestamp)
	return buf
}

func UnmarshalLatencyPing(data []byte) (*LatencyPingPayload, error) {
	if len(data) < 8 {
		return nil, errors.New("LATENCY_PING payload too short")
	}
	return &LatencyPingPayload{Timestamp: binary.BigEndian.Uint64(data[:8])}, nil
}

// LatencyPongPayload is the payload for LATENCY_PONG (0x0A) — 8 bytes.
// Contains the original PING timestamp, echoed back.
type LatencyPongPayload struct {
	Timestamp uint64 // original PING timestamp
}

func (p *LatencyPongPayload) Marshal() []byte {
	buf := make([]byte, 8)
	binary.BigEndian.PutUint64(buf, p.Timestamp)
	return buf
}

func UnmarshalLatencyPong(data []byte) (*LatencyPongPayload, error) {
	if len(data) < 8 {
		return nil, errors.New("LATENCY_PONG payload too short")
	}
	return &LatencyPongPayload{Timestamp: binary.BigEndian.Uint64(data[:8])}, nil
}

// DisconnectPayload is the payload for DISCONNECT (0x20) — 1 byte.
type DisconnectPayload struct {
	Reason uint8 // see Disconnect* constants in errors.go
}

func (p *DisconnectPayload) Marshal() []byte {
	return []byte{p.Reason}
}

func UnmarshalDisconnect(data []byte) (*DisconnectPayload, error) {
	if len(data) < 1 {
		return nil, errors.New("DISCONNECT payload too short")
	}
	return &DisconnectPayload{Reason: data[0]}, nil
}

// ErrorPayload is the payload for ERROR (0xFF) — variable length.
type ErrorPayload struct {
	Code    ErrorCode
	Message string
}

func (p *ErrorPayload) Marshal() []byte {
	msgBytes := []byte(p.Message)
	msgLen := uint8(len(msgBytes))
	buf := make([]byte, 2+msgLen)
	buf[0] = uint8(p.Code)
	buf[1] = msgLen
	copy(buf[2:], msgBytes)
	return buf
}

func UnmarshalError(data []byte) (*ErrorPayload, error) {
	if len(data) < 2 {
		return nil, errors.New("ERROR payload too short")
	}
	code := ErrorCode(data[0])
	msgLen := int(data[1])
	if len(data) < 2+msgLen {
		return nil, errors.New("ERROR message truncated")
	}
	return &ErrorPayload{
		Code:    code,
		Message: string(data[2 : 2+msgLen]),
	}, nil
}
