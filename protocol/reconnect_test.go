package protocol

import (
	"testing"
	"time"
)

func TestReconnectStateMachine(t *testing.T) {
	var transitions []string
	cb := ReconnectCallback{
		OnReconnecting: func(attempt int, backoff time.Duration) {
			transitions = append(transitions, "reconnecting")
		},
		OnConnected: func() {
			transitions = append(transitions, "connected")
		},
		OnIdle: func() {
			transitions = append(transitions, "idle")
		},
	}

	rs := NewReconnectState(cb)

	// Initial state
	if rs.State() != ConnStateIdle {
		t.Errorf("initial state: got %v, want idle", rs.State())
	}

	// Start reconnect
	backoff, ok := rs.StartReconnect()
	if !ok {
		t.Fatal("should allow first reconnect")
	}
	if backoff < 0 {
		t.Errorf("backoff should be non-negative, got %v", backoff)
	}
	if rs.State() != ConnStateReconnecting {
		t.Errorf("state: got %v, want reconnecting", rs.State())
	}

	// Connect successfully
	rs.SetConnected()
	if rs.State() != ConnStateConnected {
		t.Errorf("state: got %v, want connected", rs.State())
	}

	if len(transitions) != 2 {
		t.Errorf("transitions: got %d, want 2", len(transitions))
	}
}

func TestReconnectMaxAttempts(t *testing.T) {
	idle := false
	cb := ReconnectCallback{
		OnIdle: func() { idle = true },
	}
	rs := NewReconnectState(cb)

	for i := 0; i < ReconnectMaxAttempts; i++ {
		_, ok := rs.StartReconnect()
		if !ok && i < ReconnectMaxAttempts-1 {
			t.Fatalf("attempt %d: should allow reconnect", i)
		}
	}

	// Next attempt should fail (transitions to idle)
	_, ok := rs.StartReconnect()
	if ok {
		t.Error("should not allow reconnect after max attempts")
	}
	if !idle {
		t.Error("should have transitioned to idle")
	}
}

func TestReconnectReset(t *testing.T) {
	rs := NewReconnectState(ReconnectCallback{})
	rs.StartReconnect()
	rs.StartReconnect()

	rs.Reset()
	if rs.State() != ConnStateIdle {
		t.Errorf("state after reset: got %v, want idle", rs.State())
	}
	if rs.Attempt() != 0 {
		t.Errorf("attempt after reset: got %d, want 0", rs.Attempt())
	}
}

func TestReconnectSetConnectedResetsAttempt(t *testing.T) {
	rs := NewReconnectState(ReconnectCallback{})
	rs.StartReconnect()
	rs.StartReconnect()
	if rs.Attempt() != 2 {
		t.Errorf("attempt: got %d, want 2", rs.Attempt())
	}

	rs.SetConnected()
	if rs.Attempt() != 0 {
		t.Errorf("attempt after connect: got %d, want 0", rs.Attempt())
	}
}
