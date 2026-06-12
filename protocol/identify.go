package protocol

import (
	"encoding/binary"
	"errors"
)

// IdentifyStatus values for IDENTIFY_RESPONSE (§4.2).
const (
	IdentifyAccepted      uint8 = 0x00
	IdentifyVersionIncompat uint8 = 0x01
	IdentifyRejected      uint8 = 0x02
	IdentifyNeedConfirm   uint8 = 0x03
)

// IdentifyPayload is the IDENTIFY (0x10) frame payload (§4.2).
type IdentifyPayload struct {
	Version  uint8
	Platform Platform
	Role     Role
	Caps     CapabilityFlags
	SeqInit  uint32 // random initial sequence number
	Name     string // UTF-8 device name, \0 terminated on wire
}

// Marshal encodes the IDENTIFY payload to bytes.
func (p *IdentifyPayload) Marshal() []byte {
	nameBytes := []byte(p.Name)
	// 8 fixed bytes + name + null terminator
	buf := make([]byte, 8+len(nameBytes)+1)
	buf[0] = p.Version
	buf[1] = uint8(p.Platform)
	buf[2] = uint8(p.Role)
	buf[3] = uint8(p.Caps)
	binary.BigEndian.PutUint32(buf[4:8], p.SeqInit)
	copy(buf[8:], nameBytes)
	buf[8+len(nameBytes)] = 0 // null terminator
	return buf
}

// UnmarshalIdentify decodes an IDENTIFY payload from bytes.
func UnmarshalIdentify(data []byte) (*IdentifyPayload, error) {
	if len(data) < 8 {
		return nil, errors.New("IDENTIFY payload too short")
	}
	p := &IdentifyPayload{
		Version:  data[0],
		Platform: Platform(data[1]),
		Role:     Role(data[2]),
		Caps:     CapabilityFlags(data[3]),
		SeqInit:  binary.BigEndian.Uint32(data[4:8]),
	}
	// Find name (null-terminated)
	nameEnd := len(data)
	for i := 8; i < len(data); i++ {
		if data[i] == 0 {
			nameEnd = i
			break
		}
	}
	p.Name = string(data[8:nameEnd])
	return p, nil
}

// IdentifyResponsePayload is the IDENTIFY_RESPONSE (0x11) frame payload (§4.2).
type IdentifyResponsePayload struct {
	Status   uint8
	Platform Platform
	Caps     CapabilityFlags
	Name     string // UTF-8 device name, \0 terminated on wire
}

// Marshal encodes the IDENTIFY_RESPONSE payload to bytes.
func (p *IdentifyResponsePayload) Marshal() []byte {
	nameBytes := []byte(p.Name)
	buf := make([]byte, 3+len(nameBytes)+1)
	buf[0] = p.Status
	buf[1] = uint8(p.Platform)
	buf[2] = uint8(p.Caps)
	copy(buf[3:], nameBytes)
	buf[3+len(nameBytes)] = 0
	return buf
}

// UnmarshalIdentifyResponse decodes an IDENTIFY_RESPONSE payload.
func UnmarshalIdentifyResponse(data []byte) (*IdentifyResponsePayload, error) {
	if len(data) < 3 {
		return nil, errors.New("IDENTIFY_RESPONSE payload too short")
	}
	p := &IdentifyResponsePayload{
		Status:   data[0],
		Platform: Platform(data[1]),
		Caps:     CapabilityFlags(data[2]),
	}
	nameEnd := len(data)
	for i := 3; i < len(data); i++ {
		if data[i] == 0 {
			nameEnd = i
			break
		}
	}
	p.Name = string(data[3:nameEnd])
	return p, nil
}
