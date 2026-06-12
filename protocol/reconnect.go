package protocol

import (
	"math/rand"
	"sync"
	"time"
)

// Reconnection parameters (§10.1)
const (
	ReconnectMaxAttempts = 5
	ReconnectInitBackoff = 200 * time.Millisecond
	ReconnectMultiplier  = 2
	ReconnectMaxBackoff  = 5 * time.Second
	ReconnectJitter      = 0.2 // ±20%
)

// ConnState represents the connection state (§10.1).
type ConnState int

const (
	ConnStateIdle ConnState = iota
	ConnStateConnected
	ConnStateReconnecting
)

// String returns a human-readable state name.
func (s ConnState) String() string {
	switch s {
	case ConnStateIdle:
		return "idle"
	case ConnStateConnected:
		return "connected"
	case ConnStateReconnecting:
		return "reconnecting"
	default:
		return "unknown"
	}
}

// ReconnectState implements the reconnection state machine (§10.1).
type ReconnectState struct {
	mu       sync.Mutex
	state    ConnState
	attempt  int
	backoff  time.Duration
	callback ReconnectCallback
}

// ReconnectCallback is called on state transitions.
type ReconnectCallback struct {
	OnReconnecting func(attempt int, backoff time.Duration)
	OnConnected    func()
	OnIdle         func()
}

// NewReconnectState creates a new state machine.
func NewReconnectState(cb ReconnectCallback) *ReconnectState {
	return &ReconnectState{
		state:    ConnStateIdle,
		backoff:  ReconnectInitBackoff,
		callback: cb,
	}
}

// State returns the current connection state.
func (s *ReconnectState) State() ConnState {
	s.mu.Lock()
	defer s.mu.Unlock()
	return s.state
}

// SetConnected transitions to connected state, resetting attempt counter.
func (s *ReconnectState) SetConnected() {
	s.mu.Lock()
	s.state = ConnStateConnected
	s.attempt = 0
	s.backoff = ReconnectInitBackoff
	cb := s.callback.OnConnected
	s.mu.Unlock()

	if cb != nil {
		cb()
	}
}

// StartReconnect transitions to reconnecting state and returns the backoff duration.
// Returns false if max attempts exceeded (transitions to idle).
func (s *ReconnectState) StartReconnect() (time.Duration, bool) {
	s.mu.Lock()

	if s.attempt >= ReconnectMaxAttempts {
		s.state = ConnStateIdle
		cb := s.callback.OnIdle
		s.mu.Unlock()
		if cb != nil {
			cb()
		}
		return 0, false
	}

	s.state = ConnStateReconnecting
	s.attempt++

	// Apply jitter: ±20% of backoff
	jitter := float64(s.backoff) * ReconnectJitter
	actualBackoff := s.backoff + time.Duration(rand.Float64()*2*jitter-jitter)
	if actualBackoff < 0 {
		actualBackoff = 0
	}

	attempt := s.attempt
	cb := s.callback.OnReconnecting

	// Increase backoff for next attempt
	s.backoff = s.backoff * time.Duration(ReconnectMultiplier)
	if s.backoff > ReconnectMaxBackoff {
		s.backoff = ReconnectMaxBackoff
	}
	s.mu.Unlock()

	if cb != nil {
		cb(attempt, actualBackoff)
	}

	return actualBackoff, true
}

// Reset resets the state machine to idle.
func (s *ReconnectState) Reset() {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.state = ConnStateIdle
	s.attempt = 0
	s.backoff = ReconnectInitBackoff
}

// Attempt returns the current reconnection attempt number.
func (s *ReconnectState) Attempt() int {
	s.mu.Lock()
	defer s.mu.Unlock()
	return s.attempt
}
