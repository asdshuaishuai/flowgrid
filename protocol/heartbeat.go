package protocol

import (
	"sync"
	"time"
)

// Heartbeat timing constants (§9.1)
const (
	HeartbeatInterval = 2 * time.Second
	HeartbeatTimeout  = 3 // missed heartbeats before connection lost
	HeartbeatLossTime = HeartbeatInterval * time.Duration(HeartbeatTimeout)

	LatencyPingInterval = 2 * time.Second
)

// Latency quality thresholds (§9.2)
const (
	LatencyGoodMs = 2.0  // < 2ms: green
	LatencyWarnMs = 4.0  // 2-4ms: amber
	// > 4ms: bad (red)
	LatencyAlertThresholdMs = 5.0 // consecutive 3 > 5ms triggers UI alert
)

// LatencyQuality represents the quality rating of a latency measurement.
type LatencyQuality int

const (
	LatencyQualityGood LatencyQuality = iota
	LatencyQualityWarn
	LatencyQualityBad
)

// ClassifyLatency returns the quality rating for a latency value in milliseconds.
func ClassifyLatency(ms float64) LatencyQuality {
	switch {
	case ms < LatencyGoodMs:
		return LatencyQualityGood
	case ms < LatencyWarnMs:
		return LatencyQualityWarn
	default:
		return LatencyQualityBad
	}
}

// maxPlausibleRTTUs bounds latency samples: a PONG implying more than 10s of
// RTT is bogus (future/garbage timestamp), not a measurement.
const maxPlausibleRTTUs = 10_000_000

// LatencySample records a single latency measurement (§9.2).
type LatencySample struct {
	Timestamp time.Time
	ValueMs   float64
	Jitter    float64 // |current - previous|
}

// LatencyMonitor tracks latency samples and provides statistics (§9.2).
type LatencyMonitor struct {
	mu             sync.Mutex
	samples        []LatencySample
	maxSamples     int
	lastPing       uint64  // timestamp of last sent PING
	prevValue      float64
	consecutiveBad int // consecutive samples above LatencyAlertThresholdMs
}

// NewLatencyMonitor creates a latency monitor with the given sample buffer size.
func NewLatencyMonitor(maxSamples int) *LatencyMonitor {
	if maxSamples < 1 {
		maxSamples = 1
	}
	return &LatencyMonitor{
		samples:    make([]LatencySample, 0, maxSamples),
		maxSamples: maxSamples,
	}
}

// RecordPing records the send time of a LATENCY_PING.
func (m *LatencyMonitor) RecordPing(sendTimeUs uint64) {
	m.mu.Lock()
	defer m.mu.Unlock()
	m.lastPing = sendTimeUs
}

// RecordPong calculates latency from a LATENCY_PONG and records the sample.
// nowUs is the current time in microseconds. Returns ok=false and no sample
// for a pong that cannot be trusted: a timestamp in the future, an RTT above
// maxPlausibleRTTUs, or — once a ping time is known — a timestamp that does
// not match the last recorded ping (stale or forged pong).
func (m *LatencyMonitor) RecordPong(pongTimestampUs, nowUs uint64) (LatencySample, bool) {
	m.mu.Lock()
	defer m.mu.Unlock()

	if pongTimestampUs > nowUs {
		return LatencySample{}, false
	}
	rttUs := nowUs - pongTimestampUs
	if rttUs > maxPlausibleRTTUs {
		return LatencySample{}, false
	}
	if m.lastPing != 0 && pongTimestampUs != m.lastPing {
		return LatencySample{}, false
	}
	latencyMs := float64(rttUs) / 2000.0 // half RTT, convert µs to ms

	jitter := latencyMs - m.prevValue
	if jitter < 0 {
		jitter = -jitter
	}

	sample := LatencySample{
		Timestamp: time.Now(),
		ValueMs:   latencyMs,
		Jitter:    jitter,
	}

	m.samples = append(m.samples, sample)
	if len(m.samples) > m.maxSamples {
		m.samples = m.samples[1:]
	}
	m.prevValue = latencyMs
	if latencyMs > LatencyAlertThresholdMs {
		m.consecutiveBad++
	} else {
		m.consecutiveBad = 0
	}

	return sample, true
}

// ConsecutiveBad returns the number of consecutive samples exceeding
// LatencyAlertThresholdMs (§9.2: 3 in a row triggers the UI alert banner).
func (m *LatencyMonitor) ConsecutiveBad() int {
	m.mu.Lock()
	defer m.mu.Unlock()
	return m.consecutiveBad
}

// Latest returns the most recent sample, or nil if none.
func (m *LatencyMonitor) Latest() *LatencySample {
	m.mu.Lock()
	defer m.mu.Unlock()
	if len(m.samples) == 0 {
		return nil
	}
	return &m.samples[len(m.samples)-1]
}

// Stats returns min, max, and average latency.
func (m *LatencyMonitor) Stats() (min, max, avg float64) {
	m.mu.Lock()
	defer m.mu.Unlock()
	if len(m.samples) == 0 {
		return 0, 0, 0
	}
	min = m.samples[0].ValueMs
	max = m.samples[0].ValueMs
	sum := 0.0
	for _, s := range m.samples {
		if s.ValueMs < min {
			min = s.ValueMs
		}
		if s.ValueMs > max {
			max = s.ValueMs
		}
		sum += s.ValueMs
	}
	avg = sum / float64(len(m.samples))
	return
}

// HeartbeatTracker monitors heartbeat liveness (§9.1).
type HeartbeatTracker struct {
	mu               sync.Mutex
	missed           int
	lostFired        bool // onConnectionLost fires once per loss episode
	onConnectionLost func()
}

// NewHeartbeatTracker creates a new heartbeat tracker.
// onLost is called when heartbeat timeout is detected.
func NewHeartbeatTracker(onLost func()) *HeartbeatTracker {
	return &HeartbeatTracker{
		onConnectionLost: onLost,
	}
}

// ReceivedHeartbeat records a received heartbeat and re-arms the lost callback.
func (t *HeartbeatTracker) ReceivedHeartbeat() {
	t.mu.Lock()
	defer t.mu.Unlock()
	t.missed = 0
	t.lostFired = false
}

// Tick should be called every HeartbeatInterval.
// Returns true once the connection is considered lost (missed >= timeout).
// The onConnectionLost callback fires once per loss episode — subsequent ticks
// stay lost without re-firing — and is re-armed by ReceivedHeartbeat/Reset.
// The callback runs without the lock held so it may call back into the tracker.
func (t *HeartbeatTracker) Tick() bool {
	t.mu.Lock()
	t.missed++
	if t.missed < HeartbeatTimeout {
		t.mu.Unlock()
		return false
	}
	fire := !t.lostFired
	t.lostFired = true
	cb := t.onConnectionLost
	t.mu.Unlock()

	if fire && cb != nil {
		cb()
	}
	return true
}

// Reset resets the tracker.
func (t *HeartbeatTracker) Reset() {
	t.mu.Lock()
	defer t.mu.Unlock()
	t.missed = 0
	t.lostFired = false
}
