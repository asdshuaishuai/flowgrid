package protocol

import (
	"testing"
)

func TestLatencyMonitorRecordPong(t *testing.T) {
	m := NewLatencyMonitor(100)

	// Simulate: PING sent at t=1000µs, PONG received at t=3000µs
	// RTT = 2000µs, latency = 1000µs = 1.0ms
	sample := m.RecordPong(1000, 3000)
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
	sample := m.RecordPong(1000, 5000)
	if sample.Jitter != 1.0 {
		t.Errorf("jitter: got %f, want 1.0", sample.Jitter)
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
