package protocol

import "sync"

// SequenceTracker manages UInt16 sequence numbers with gap detection (§10.3).
type SequenceTracker struct {
	mu       sync.Mutex
	next     uint16
	expected uint16
	init     uint16
	started  bool
}

// NewSequenceTracker creates a tracker with the given initial sequence number.
func NewSequenceTracker(seqInit uint32) *SequenceTracker {
	return &SequenceTracker{
		next:     uint16(seqInit & 0xFFFF),
		expected: uint16(seqInit & 0xFFFF),
		init:     uint16(seqInit & 0xFFFF),
	}
}

// Next returns the next sequence number to use when sending a frame.
// Wraps around at 65535 to 0.
func (t *SequenceTracker) Next() uint16 {
	t.mu.Lock()
	defer t.mu.Unlock()
	seq := t.next
	t.next++
	return seq
}

// Check verifies a received sequence number and detects gaps.
// Returns nil if sequential, or an error describing the gap.
func (t *SequenceTracker) Check(received uint16) error {
	t.mu.Lock()
	defer t.mu.Unlock()

	if !t.started {
		t.started = true
		t.expected = received + 1
		return nil
	}

	diff := received - t.expected
	if diff == 0 {
		// Expected sequence
		t.expected++
		return nil
	}
	if diff > 0 && diff < 0x8000 {
		// Frame(s) skipped
		t.expected = received + 1
		return NewError(ErrSequenceGap, "seq_gap")
	}
	// diff >= 0x8000 means received is behind expected (old/retransmitted frame) — ignore
	return nil
}

// Reset reinitializes the tracker with a new sequence init value.
func (t *SequenceTracker) Reset(seqInit uint32) {
	t.mu.Lock()
	defer t.mu.Unlock()
	t.next = uint16(seqInit & 0xFFFF)
	t.expected = uint16(seqInit & 0xFFFF)
	t.init = uint16(seqInit & 0xFFFF)
	t.started = false
}

// Current returns the current next-to-send sequence number without advancing.
func (t *SequenceTracker) Current() uint16 {
	t.mu.Lock()
	defer t.mu.Unlock()
	return t.next
}
