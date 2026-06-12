package protocol

import (
	"testing"
)

func TestSequenceTrackerBasic(t *testing.T) {
	tr := NewSequenceTracker(100)
	if tr.Current() != 100 {
		t.Errorf("initial: got %d, want 100", tr.Current())
	}
	if tr.Next() != 100 {
		t.Errorf("first next: got %d, want 100", tr.Next())
	}
	if tr.Next() != 101 {
		t.Errorf("second next: got %d, want 101", tr.Next())
	}
}

func TestSequenceTrackerWrapAround(t *testing.T) {
	tr := NewSequenceTracker(65534)
	tr.Next() // 65534
	tr.Next() // 65535
	got := tr.Next() // should wrap to 0
	if got != 0 {
		t.Errorf("wrap: got %d, want 0", got)
	}
}

func TestSequenceTrackerCheckSequential(t *testing.T) {
	tr := NewSequenceTracker(0)
	// First frame starts tracking
	if err := tr.Check(0); err != nil {
		t.Errorf("Check(0): %v", err)
	}
	if err := tr.Check(1); err != nil {
		t.Errorf("Check(1): %v", err)
	}
	if err := tr.Check(2); err != nil {
		t.Errorf("Check(2): %v", err)
	}
}

func TestSequenceTrackerCheckGap(t *testing.T) {
	tr := NewSequenceTracker(0)
	tr.Check(0) // expected=1
	err := tr.Check(3) // gap of 2
	if err == nil {
		t.Error("expected gap error")
	}
	if pe, ok := err.(*ProtocolError); ok {
		if pe.Code != ErrSequenceGap {
			t.Errorf("error code: got 0x%02X, want 0x%02X", pe.Code, ErrSequenceGap)
		}
	}
}

func TestSequenceTrackerReset(t *testing.T) {
	tr := NewSequenceTracker(0)
	tr.Next() // 0
	tr.Next() // 1
	tr.Reset(500)
	if tr.Current() != 500 {
		t.Errorf("after reset: got %d, want 500", tr.Current())
	}
}
