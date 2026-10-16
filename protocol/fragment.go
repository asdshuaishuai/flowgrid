package protocol

import (
	"encoding/binary"
	"errors"
	"fmt"
	"sync"
	"time"
)

// Fragment header constants (§7.3)
const (
	FragHeaderSize = 4
	FragTimeout    = 5 * time.Second
	DefaultMTU     = 1400 // NearLink WiFi Direct safe MTU

	// maxConcurrentGroups bounds reassembly memory against a flood of
	// never-completing fragment groups: once the cap is reached, the oldest
	// incomplete group is dropped to make room.
	maxConcurrentGroups = 32
)

// FragHeader is the 4-byte fragmentation header prepended to fragmented frames (§7.3).
//
//	Offset 0: FragSeq (UInt16 BE) — fragment group sequence number
//	Offset 2: FragIdx (UInt8)     — current fragment index (0-based)
//	Offset 3: FragTot (UInt8)     — total fragment count
type FragHeader struct {
	FragSeq uint16
	FragIdx uint8
	FragTot uint8
}

// MarshalFragHeader encodes the 4-byte fragment header.
func (h *FragHeader) Marshal() []byte {
	buf := make([]byte, FragHeaderSize)
	binary.BigEndian.PutUint16(buf[0:2], h.FragSeq)
	buf[2] = h.FragIdx
	buf[3] = h.FragTot
	return buf
}

// UnmarshalFragHeader decodes a 4-byte fragment header.
func UnmarshalFragHeader(data []byte) (*FragHeader, error) {
	if len(data) < FragHeaderSize {
		return nil, errors.New("fragment header too short")
	}
	return &FragHeader{
		FragSeq: binary.BigEndian.Uint16(data[0:2]),
		FragIdx: data[2],
		FragTot: data[3],
	}, nil
}

// FragmentFrame splits a HIDFrame into fragments if it exceeds the MTU.
// Returns the fragment payloads (each is FragHeader + a chunk of the frame
// bytes) and a nil error when fragmentation was needed, or (nil, nil) when the
// frame fits in one packet and should be sent as-is. A non-nil error means the
// frame cannot go on the wire at all (payload over the 255-byte protocol
// limit, MTU too small, or more than 255 fragments required).
func FragmentFrame(frame *HIDFrame, mtu int) ([][]byte, error) {
	frameBytes, err := frame.EncodeBytes()
	if err != nil {
		return nil, err
	}
	if mtu <= FragHeaderSize {
		return nil, NewError(ErrInvalidPayload, fmt.Sprintf("mtu %d too small for fragmentation", mtu))
	}

	maxPayload := mtu - FragHeaderSize
	if len(frameBytes) <= maxPayload {
		return nil, nil // no fragmentation needed
	}

	total := (len(frameBytes) + maxPayload - 1) / maxPayload
	if total > 255 {
		return nil, NewError(ErrInvalidPayload, fmt.Sprintf("frame needs %d fragments (max 255)", total))
	}
	fragments := make([][]byte, 0, total)

	for i := 0; i < total; i++ {
		start := i * maxPayload
		end := start + maxPayload
		if end > len(frameBytes) {
			end = len(frameBytes)
		}

		hdr := FragHeader{
			FragSeq: 0, // caller should set this
			FragIdx: uint8(i),
			FragTot: uint8(total),
		}
		frag := append(hdr.Marshal(), frameBytes[start:end]...)
		fragments = append(fragments, frag)
	}

	return fragments, nil
}

// fragGroup buffers incoming fragments until all arrive.
type fragGroup struct {
	fragments [][]byte
	total     uint8
	received  int
	createdAt time.Time
}

// FragmentReassembler reassembles fragmented HIDFrames (§7.3).
type FragmentReassembler struct {
	mu    sync.Mutex
	groups map[uint16]*fragGroup
}

// NewFragmentReassembler creates a new reassembler.
func NewFragmentReassembler() *FragmentReassembler {
	return &FragmentReassembler{
		groups: make(map[uint16]*fragGroup),
	}
}

// Feed adds a fragment payload (including the 4-byte frag header).
// Returns the reassembled HIDFrame bytes when all fragments are collected, or nil if more are needed.
// Returns an error if the fragment is invalid or inconsistent with the group
// it belongs to (mismatched FragTot would otherwise corrupt or overflow the
// group buffer indexed by FragIdx).
func (r *FragmentReassembler) Feed(data []byte) ([]byte, error) {
	hdr, err := UnmarshalFragHeader(data)
	if err != nil {
		return nil, err
	}
	if hdr.FragIdx >= hdr.FragTot || hdr.FragTot == 0 {
		return nil, NewError(ErrInvalidPayload, "invalid fragment index/total")
	}

	// Copy the fragment bytes: the caller's buffer may be freed as soon as
	// this call returns (cgo callers own their memory), while the group keeps
	// the payload alive until reassembly completes.
	payload := make([]byte, len(data)-FragHeaderSize)
	copy(payload, data[FragHeaderSize:])

	r.mu.Lock()
	defer r.mu.Unlock()

	g, exists := r.groups[hdr.FragSeq]
	if exists {
		if hdr.FragTot != g.total {
			return nil, NewError(ErrInvalidPayload, "fragment total mismatch for group")
		}
	} else {
		r.evictLocked()
		g = &fragGroup{
			fragments: make([][]byte, hdr.FragTot),
			total:     hdr.FragTot,
			createdAt: time.Now(),
		}
		r.groups[hdr.FragSeq] = g
	}

	if g.fragments[hdr.FragIdx] != nil {
		return nil, nil // duplicate, ignore
	}

	g.fragments[hdr.FragIdx] = payload
	g.received++

	if g.received == int(g.total) {
		// All fragments received — reassemble
		var assembled []byte
		for i := 0; i < int(g.total); i++ {
			assembled = append(assembled, g.fragments[i]...)
		}
		delete(r.groups, hdr.FragSeq)
		return assembled, nil
	}

	return nil, nil
}

// evictLocked drops expired groups, then the oldest incomplete groups beyond
// maxConcurrentGroups. Caller must hold r.mu.
func (r *FragmentReassembler) evictLocked() {
	now := time.Now()
	for seq, g := range r.groups {
		if now.Sub(g.createdAt) > FragTimeout {
			delete(r.groups, seq)
		}
	}
	for len(r.groups) >= maxConcurrentGroups {
		oldestSeq := uint16(0)
		var oldest time.Time
		first := true
		for seq, g := range r.groups {
			if first || g.createdAt.Before(oldest) {
				oldestSeq, oldest, first = seq, g.createdAt, false
			}
		}
		delete(r.groups, oldestSeq)
	}
}

// Cleanup removes fragment groups that have timed out.
func (r *FragmentReassembler) Cleanup() {
	r.mu.Lock()
	defer r.mu.Unlock()
	now := time.Now()
	for seq, g := range r.groups {
		if now.Sub(g.createdAt) > FragTimeout {
			delete(r.groups, seq)
		}
	}
}
