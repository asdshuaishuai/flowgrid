package protocol

import (
	"testing"
)

func TestLatencyMonitorRecordPong(t *testing.T) {
	m := NewLatencyMonitor(100)

	// Simulate: PING sent at t=1000µs, PONG received at t=3000µs
	// RTT = 2000µs, latency = 1000µs = 1.0ms
	sample, ok := m.RecordPong(1000, 3000)
	if !ok {
		t.Fatal("expected sample to be accepted")
	}
	if sample.ValueMs != 1.0 {
		t.Errorf("latency: got %f, want 1.0", sample.ValueMs)
	}

	latest := m.Latest()
	if latest == nil {
		t.Fatal("expected latest sample")
	}
	if latest.ValueMs != 1.0 {
		t.Errorf("latest: got %f, want 1.0", latest.ValueMs)
	}
}

func TestLatencyMonitorJitter(t *testing.T) {
	m := NewLatencyMonitor(100)

	// First sample: 1.0ms
	m.RecordPong(1000, 3000)
	// Second sample: 2.0ms -> jitter = |2.0 - 1.0| = 1.0
	sample, ok := m.RecordPong(1000, 5000)
	if !ok {
		t.Fatal("expected sample to be accepted")
	}
	if sample.Jitter != 1.0 {
		t.Errorf("jitter: got %f, want 1.0", sample.Jitter)
	}
}

// A PONG whose timestamp lies in the future (uint64 underflow risk), implies
// an absurd RTT, or does not match the recorded ping must be rejected — it
// would otherwise poison latency stats and trigger false alert banners.
func TestLatencyMonitorRejectsBogusPong(t *testing.T) {
	m := NewLatencyMonitor(10)
	m.RecordPing(5000)

	if _, ok := m.RecordPong(9000, 8000); ok {
		t.Error("future timestamp (pong > now) must be rejected")
	}
	if _, ok := m.RecordPong(0, 999999999); ok {
		t.Error("RTT above the plausibility cap must be rejected")
	}
	if _, ok := m.RecordPong(4321, 64321); ok {
		t.Error("pong not matching the recorded ping must be rejected")
	}
	sample, ok := m.RecordPong(5000, 7000) // RTT 2000µs → 1.0ms, matches ping
	if !ok || sample.ValueMs != 1.0 {
		t.Errorf("matching pong should be accepted: ok=%v sample=%+v", ok, sample)
	}
}

func TestLatencyMonitorStats(t *testing.T) {
	m := NewLatencyMonitor(100)

	// Add samples: 1ms, 2ms, 3ms
	m.RecordPong(0, 2000)  // 1.0ms
	m.RecordPong(0, 4000)  // 2.0ms
	m.RecordPong(0, 6000)  // 3.0ms

	min, max, avg := m.Stats()
	if min != 1.0 {
		t.Errorf("min: got %f, want 1.0", min)
	}
	if max != 3.0 {
		t.Errorf("max: got %f, want 3.0", max)
	}
	if avg != 2.0 {
		t.Errorf("avg: got %f, want 2.0", avg)
	}
}

func TestClassifyLatency(t *testing.T) {
	tests := []struct {
		ms   float64
		want LatencyQuality
	}{
		{0.5, LatencyQualityGood},
		{1.9, LatencyQualityGood},
		{2.0, LatencyQualityWarn},
		{3.5, LatencyQualityWarn},
		{4.0, LatencyQualityBad},
		{10.0, LatencyQualityBad},
	}
	for _, tt := range tests {
		got := ClassifyLatency(tt.ms)
		if got != tt.want {
			t.Errorf("ClassifyLatency(%f): got %d, want %d", tt.ms, got, tt.want)
		}
	}
}

func TestHeartbeatTrackerTimeout(t *testing.T) {
	lost := false
	tracker := NewHeartbeatTracker(func() { lost = true })

	// Simulate 3 missed heartbeats
	for i := 0; i < 3; i++ {
		tracker.Tick()
	}

	if !lost {
		t.Error("expected connection lost after 3 missed heartbeats")
	}
}

// The lost callback fires once per loss episode: ticks after the timeout keep
// reporting lost but must not re-fire the callback, and a received heartbeat
// re-arms it.
func TestHeartbeatTrackerFiresOncePerEpisode(t *testing.T) {
	fired := 0
	tracker := NewHeartbeatTracker(func() { fired++ })

	for i := 0; i < 6; i++ {
		lost := tracker.Tick()
		if want := i >= 2; lost != want {
			t.Fatalf("tick %d: lost=%v, want %v", i, lost, want)
		}
	}
	if fired != 1 {
		t.Fatalf("callback fired %d times, want 1", fired)
	}

	tracker.ReceivedHeartbeat()
	if tracker.Tick() {
		t.Error("should not be lost right after a heartbeat")
	}
	tracker.Tick()
	tracker.Tick()
	tracker.Tick()
	if fired != 2 {
		t.Fatalf("callback fired %d times after re-arm, want 2", fired)
	}
}

// The lost callback must run without the tracker lock held so it can call
// back into the tracker (e.g. Reset) without deadlocking.
func TestHeartbeatTrackerCallbackMayReenter(t *testing.T) {
	done := make(chan struct{})
	var tracker *HeartbeatTracker
	tracker = NewHeartbeatTracker(func() {
		tracker.Reset() // would deadlock if the callback held the lock
		close(done)
	})
	tracker.Tick()
	tracker.Tick()
	tracker.Tick()
	select {
	case <-done:
	default:
		t.Fatal("callback deadlocked or did not run")
	}
}

func TestHeartbeatTrackerRecover(t *testing.T) {
	lost := false
	tracker := NewHeartbeatTracker(func() { lost = true })

	tracker.Tick() // missed 1
	tracker.ReceivedHeartbeat() // reset
	tracker.Tick() // missed 1 (not 2)

	if lost {
		t.Error("should not be lost after heartbeat recovery")
	}
}

func TestLatencyMonitorMaxSamples(t *testing.T) {
	m := NewLatencyMonitor(3)
	for i := 0; i < 5; i++ {
		m.RecordPong(0, uint64((i+1)*2000))
	}
	if len(m.samples) != 3 {
		t.Errorf("samples: got %d, want 3", len(m.samples))
	}
}

func TestLatencyConsecutiveBad(t *testing.T) {
	m := NewLatencyMonitor(10)

	// (now-pong)=12000µs → 6ms: above the 5ms alert threshold.
	bad := func(now uint64) { m.RecordPong(now-12000, now) }
	// (now-pong)=2000µs → 1ms: healthy.
	good := func(now uint64) { m.RecordPong(now-2000, now) }

	var now uint64 = 1000000
	good(now)
	if m.ConsecutiveBad() != 0 {
		t.Fatalf("healthy sample: consecutive bad = %d, want 0", m.ConsecutiveBad())
	}

	now += 100000
	bad(now)
	now += 100000
	bad(now)
	if m.ConsecutiveBad() != 2 {
		t.Fatalf("two bad samples: consecutive bad = %d, want 2", m.ConsecutiveBad())
	}

	now += 100000
	good(now)
	if m.ConsecutiveBad() != 0 {
		t.Fatalf("healthy sample resets streak: got %d, want 0", m.ConsecutiveBad())
	}

	now += 100000
	bad(now)
	now += 100000
	bad(now)
	now += 100000
	bad(now)
	if m.ConsecutiveBad() != 3 {
		t.Fatalf("three bad samples: consecutive bad = %d, want 3", m.ConsecutiveBad())
	}
}
