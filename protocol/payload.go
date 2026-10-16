package protocol

import (
	"encoding/binary"
	"fmt"
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
		return nil, NewError(ErrInvalidPayload, "KEY_DOWN payload too short")
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
		return nil, NewError(ErrInvalidPayload, "KEY_UP payload too short")
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
		return nil, NewError(ErrInvalidPayload, "MOUSE_MOVE payload too short")
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

// Validate rejects button IDs and states outside the §5.3 MOUSE_BTN table.
func (p *MouseButtonPayload) Validate() error {
	if p.ButtonID < MouseButtonLeft || p.ButtonID > MouseButtonForward {
		return NewError(ErrInvalidPayload, fmt.Sprintf("unknown mouse button id 0x%02X", p.ButtonID))
	}
	if p.State > 1 {
		return NewError(ErrInvalidPayload, fmt.Sprintf("invalid mouse button state 0x%02X", p.State))
	}
	return nil
}

func UnmarshalMouseButton(data []byte) (*MouseButtonPayload, error) {
	if len(data) < 2 {
		return nil, NewError(ErrInvalidPayload, "MOUSE_BTN payload too short")
	}
	p := &MouseButtonPayload{
		ButtonID: data[0],
		State:    data[1],
	}
	if err := p.Validate(); err != nil {
		return nil, err
	}
	return p, nil
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
		return nil, NewError(ErrInvalidPayload, "SCROLL payload too short")
	}
	return &ScrollPayload{
		DeltaY: int16(binary.BigEndian.Uint16(data[0:2])),
		DeltaX: int16(binary.BigEndian.Uint16(data[2:4])),
	}, nil
}

// ClipboardPayload is the payload for CLIPBOARD (0x07) — variable length (v1.1 §5.3).
// The wire format embeds segmentation: TotalSize counts the whole clipboard,
// FragIdx/FragTot address this frame's chunk of it. A clipboard that fits one
// frame is a single fragment (FragTot=1). The v1.0 "unsegmented full data"
// layout is gone — it could never carry more than ~250 bytes against the 64KB
// target because of the UInt8 frame payload limit.
type ClipboardPayload struct {
	MIMEType  string // e.g. "text/plain"
	TotalSize uint32 // full clipboard size across all fragments (max 63240)
	FragIdx   uint8  // 0-based index of this chunk
	FragTot   uint8  // total fragment count (>= 1)
	Data      []byte // this frame's chunk
}

const (
	// MaxClipboardSize is the absolute wire cap: 255 fragments × the largest
	// chunk (248 bytes, empty MIME). The v1.0 "64KB" figure was unreachable
	// under the UInt8 fragment count and is corrected here.
	MaxClipboardSize = 255 * (int(MaxPayloadLen) - clipboardFixedLen) // 63240
	// clipboardFixedLen is the per-frame overhead: MIME len byte + 4-byte
	// TotalSize + FragIdx + FragTot (the MIME itself is extra).
	clipboardFixedLen = 7
)

// MaxClipboardChunk returns the largest Data chunk a single CLIPBOARD frame
// can carry for a MIME type of the given encoded length (§5.3).
func MaxClipboardChunk(mimeLen int) int {
	if mimeLen < 0 {
		return 0
	}
	return int(MaxPayloadLen) - clipboardFixedLen - mimeLen
}

// SegmentClipboard splits a whole clipboard into per-frame payloads (§5.3).
// Each returned payload carries one chunk with its own FragIdx/FragTot; send
// them in order as CLIPBOARD frames.
func SegmentClipboard(mime string, data []byte) ([]ClipboardPayload, error) {
	if uint64(len(data)) > uint64(MaxClipboardSize) {
		return nil, NewError(ErrInvalidPayload, fmt.Sprintf("clipboard too large: %d bytes (max %d)", len(data), MaxClipboardSize))
	}
	chunk := MaxClipboardChunk(len(mime))
	if chunk <= 0 {
		return nil, NewError(ErrInvalidPayload, fmt.Sprintf("MIME type too long for clipboard frames: %d bytes", len(mime)))
	}
	fragTot := (len(data) + chunk - 1) / chunk
	if fragTot == 0 {
		fragTot = 1 // empty clipboard is one empty chunk
	}
	if fragTot > 255 {
		return nil, NewError(ErrInvalidPayload, fmt.Sprintf("clipboard needs %d fragments (max 255); raise per-frame chunking", fragTot))
	}
	out := make([]ClipboardPayload, 0, fragTot)
	for i := 0; i < fragTot; i++ {
		start := i * chunk
		end := start + chunk
		if end > len(data) {
			end = len(data)
		}
		out = append(out, ClipboardPayload{
			MIMEType:  mime,
			TotalSize: uint32(len(data)),
			FragIdx:   uint8(i),
			FragTot:   uint8(fragTot),
			Data:      data[start:end],
		})
	}
	return out, nil
}

func (p *ClipboardPayload) Marshal() ([]byte, error) {
	mimeBytes := []byte(p.MIMEType)
	if len(mimeBytes) > 255 {
		return nil, NewError(ErrInvalidPayload, fmt.Sprintf("MIME type too long: %d bytes (max 255)", len(mimeBytes)))
	}
	if p.FragTot == 0 {
		return nil, NewError(ErrInvalidPayload, "clipboard FragTot must be >= 1")
	}
	if p.FragIdx >= p.FragTot {
		return nil, NewError(ErrInvalidPayload, fmt.Sprintf("clipboard FragIdx %d out of range for FragTot %d", p.FragIdx, p.FragTot))
	}
	// Convenience: a single-fragment clipboard derives its total size.
	if p.FragTot == 1 && p.TotalSize == 0 {
		p.TotalSize = uint32(len(p.Data))
	}
	if uint64(len(p.Data)) > uint64(MaxClipboardSize) {
		return nil, NewError(ErrInvalidPayload, fmt.Sprintf("clipboard chunk too large: %d bytes", len(p.Data)))
	}
	if len(p.Data) > MaxClipboardChunk(len(mimeBytes)) {
		return nil, NewError(ErrInvalidPayload, fmt.Sprintf("clipboard chunk %d exceeds per-frame budget %d; use SegmentClipboard", len(p.Data), MaxClipboardChunk(len(mimeBytes))))
	}
	if uint64(p.TotalSize) < uint64(len(p.Data)) {
		return nil, NewError(ErrInvalidPayload, "clipboard TotalSize smaller than chunk")
	}
	// Convenience: a single-fragment clipboard derives its total size.
	if p.FragTot == 1 && p.TotalSize == 0 {
		p.TotalSize = uint32(len(p.Data))
	}

	buf := make([]byte, clipboardFixedLen+len(mimeBytes)+len(p.Data))
	buf[0] = uint8(len(mimeBytes))
	copy(buf[1:1+len(mimeBytes)], mimeBytes)
	binary.BigEndian.PutUint32(buf[1+len(mimeBytes):5+len(mimeBytes)], p.TotalSize)
	buf[5+len(mimeBytes)] = p.FragIdx
	buf[6+len(mimeBytes)] = p.FragTot
	copy(buf[7+len(mimeBytes):], p.Data)
	return buf, nil
}

func UnmarshalClipboard(data []byte) (*ClipboardPayload, error) {
	if len(data) < clipboardFixedLen {
		return nil, NewError(ErrInvalidPayload, "CLIPBOARD payload too short")
	}
	mimeLen := int(data[0])
	if len(data) < clipboardFixedLen+mimeLen {
		return nil, NewError(ErrInvalidPayload, "CLIPBOARD payload truncated")
	}
	mimeType := string(data[1 : 1+mimeLen])
	totalSize := binary.BigEndian.Uint32(data[1+mimeLen : 5+mimeLen])
	fragIdx, fragTot := data[5+mimeLen], data[6+mimeLen]
	if fragTot == 0 || fragIdx >= fragTot {
		return nil, NewError(ErrInvalidPayload, "CLIPBOARD fragment index/total invalid")
	}
	if uint64(totalSize) > uint64(MaxClipboardSize) {
		return nil, NewError(ErrInvalidPayload, fmt.Sprintf("CLIPBOARD total size %d exceeds limit", totalSize))
	}
	chunkLen := len(data) - clipboardFixedLen - mimeLen
	if chunkLen > MaxClipboardChunk(mimeLen) {
		return nil, NewError(ErrInvalidPayload, "CLIPBOARD chunk exceeds per-frame budget")
	}
	if uint64(totalSize) < uint64(chunkLen) {
		return nil, NewError(ErrInvalidPayload, "CLIPBOARD total size smaller than chunk")
	}
	clipData := make([]byte, chunkLen)
	copy(clipData, data[clipboardFixedLen+mimeLen:])
	return &ClipboardPayload{
		MIMEType:  mimeType,
		TotalSize: totalSize,
		FragIdx:   fragIdx,
		FragTot:   fragTot,
		Data:      clipData,
	}, nil
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
		return nil, NewError(ErrInvalidPayload, "LATENCY_PING payload too short")
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
		return nil, NewError(ErrInvalidPayload, "LATENCY_PONG payload too short")
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
		return nil, NewError(ErrInvalidPayload, "DISCONNECT payload too short")
	}
	return &DisconnectPayload{Reason: data[0]}, nil
}

// ErrorPayload is the payload for ERROR (0xFF) — variable length.
type ErrorPayload struct {
	Code    ErrorCode
	Message string
}

// MaxErrorMessageLen is the maximum error message length (255 - 2 header bytes).
const MaxErrorMessageLen = int(MaxPayloadLen) - 2

func (p *ErrorPayload) Marshal() ([]byte, error) {
	msgBytes := []byte(p.Message)
	if len(msgBytes) > MaxErrorMessageLen {
		return nil, NewError(ErrInvalidPayload, fmt.Sprintf("error message too long: %d bytes (max %d)", len(msgBytes), MaxErrorMessageLen))
	}
	msgLen := uint8(len(msgBytes))
	buf := make([]byte, 2+msgLen)
	buf[0] = uint8(p.Code)
	buf[1] = msgLen
	copy(buf[2:], msgBytes)
	return buf, nil
}

func UnmarshalError(data []byte) (*ErrorPayload, error) {
	if len(data) < 2 {
		return nil, NewError(ErrInvalidPayload, "ERROR payload too short")
	}
	code := ErrorCode(data[0])
	msgLen := int(data[1])
	if len(data) < 2+msgLen {
		return nil, NewError(ErrInvalidPayload, "ERROR message truncated")
	}
	return &ErrorPayload{
		Code:    code,
		Message: string(data[2 : 2+msgLen]),
	}, nil
}
