//! Wrapper around `protocol_ffi::LatencyMonitor` with additional sampling
//! and signal-style events using `tokio::sync::watch`.

use crate::error::Result;
use crate::protocol_ffi::LatencyMonitor as FfiLatencyMonitor;
use std::sync::Arc;
use tokio::sync::watch;
use tracing::{debug, trace, warn};

/// A latency sample with jitter and quality assessment.
#[derive(Debug, Clone, Copy, Default)]
pub struct LatencySample {
    pub latency_ms: f64,
    pub jitter_ms: f64,
    pub quality: LatencyQuality,
}

/// Quality assessment based on latency thresholds.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub enum LatencyQuality {
    #[default]
    Unknown,
    Good,  // < 2ms
    Warn,  // 2-4ms
    Bad,   // > 4ms
}

impl LatencyQuality {
    pub fn from_latency_ms(ms: f64) -> Self {
        if ms < 2.0 {
            Self::Good
        } else if ms <= 4.0 {
            Self::Warn
        } else {
            Self::Bad
        }
    }

    pub fn as_str(&self) -> &'static str {
        match self {
            LatencyQuality::Unknown => "unknown",
            LatencyQuality::Good => "good",
            LatencyQuality::Warn => "warn",
            LatencyQuality::Bad => "bad",
        }
    }
}

/// Shared state broadcast via `tokio::sync::watch`.
#[derive(Debug, Clone, Default)]
pub struct LatencyState {
    pub latest: LatencySample,
    pub stats: LatencyStats,
}

/// Aggregated statistics from the FFI monitor.
#[derive(Debug, Clone, Copy, Default)]
pub struct LatencyStats {
    pub min_ms: f64,
    pub max_ms: f64,
    pub avg_ms: f64,
}

/// Tokio-aware latency monitor wrapping the FFI object.
pub struct TokioLatencyMonitor {
    inner: FfiLatencyMonitor,
    tx: watch::Sender<LatencyState>,
    rx: watch::Receiver<LatencyState>,
}

impl TokioLatencyMonitor {
    pub fn new(max_samples: i32) -> Self {
        let inner = FfiLatencyMonitor::new(max_samples);
        let state = LatencyState::default();
        let (tx, rx) = watch::channel(state);
        Self { inner, tx, rx }
    }

    /// Record a ping send time (microseconds).
    pub fn record_ping(&self, send_time_us: u64) {
        self.inner.record_ping(send_time_us);
        trace!("Latency ping recorded at {send_time_us} us");
    }

    /// Record a pong response and broadcast the updated state.
    pub fn record_pong(&self, pong_timestamp_us: u64, now_us: u64) -> Result<LatencySample> {
        let (ms, jitter) = self.inner.record_pong(pong_timestamp_us, now_us)?;
        let quality = LatencyQuality::from_latency_ms(ms);
        let sample = LatencySample { latency_ms: ms, jitter_ms: jitter, quality };

        let stats = self.inner.stats().unwrap_or((0.0, 0.0, 0.0));
        let state = LatencyState {
            latest: sample,
            stats: LatencyStats {
                min_ms: stats.0,
                max_ms: stats.1,
                avg_ms: stats.2,
            },
        };

        if self.tx.send(state).is_err() {
            warn!("No latency state subscribers");
        }

        debug!(
            "Latency updated: {:.2} ms (jitter {:.2} ms, quality: {})",
            ms,
            jitter,
            quality.as_str()
        );
        Ok(sample)
    }

    /// Get the latest sample without triggering a broadcast.
    pub fn latest(&self) -> Result<LatencySample> {
        let (ms, jitter, quality_code) = self.inner.latest()?;
        let quality = match quality_code {
            0 => LatencyQuality::Good,
            1 => LatencyQuality::Warn,
            2 => LatencyQuality::Bad,
            _ => LatencyQuality::Unknown,
        };
        Ok(LatencySample { latency_ms: ms, jitter_ms: jitter, quality })
    }

    /// Subscribe to latency state changes.
    pub fn subscribe(&self) -> watch::Receiver<LatencyState> {
        self.rx.clone()
    }

    /// Return a reference to the internal watch sender (for advanced use).
    pub fn state_tx(&self) -> &watch::Sender<LatencyState> {
        &self.tx
    }
}

impl Default for TokioLatencyMonitor {
    fn default() -> Self {
        Self::new(100)
    }
}

/// Convenience: create a shared Arc monitor for use across tasks.
pub fn create_shared_monitor(max_samples: i32) -> Arc<TokioLatencyMonitor> {
    Arc::new(TokioLatencyMonitor::new(max_samples))
}
