//! Mouse interpolation smoothing (Protocol Appendix C).
use std::collections::VecDeque;

const HISTORY_SIZE: usize = 5;
const JITTER_THRESHOLD: f64 = 2.0; // px
const SMOOTH_FACTOR: f64 = 0.3;
const PREDICT_FRAME_TIME_MS: f64 = 16.67; // 60fps
const MAX_INTERP_DISTANCE: f64 = 5.0; // px

#[derive(Debug, Clone, Copy)]
struct Sample {
    x: f64,
    y: f64,
    #[allow(dead_code)]
    t: f64, // ms since start
}

pub struct MouseInterpolator {
    history: VecDeque<Sample>,
    last_time: f64,
}

impl MouseInterpolator {
    pub fn new() -> Self {
        Self {
            history: VecDeque::with_capacity(HISTORY_SIZE),
            last_time: 0.0,
        }
    }

    pub fn process(&mut self, dx: i16, dy: i16, timestamp_ms: f64) -> Vec<(i16, i16)> {
        let raw_dx = dx as f64;
        let raw_dy = dy as f64;

        // Jitter threshold: ignore tiny movements
        if raw_dx.abs() < JITTER_THRESHOLD && raw_dy.abs() < JITTER_THRESHOLD {
            return vec![];
        }

        // Add to history
        let current = Sample {
            x: raw_dx,
            y: raw_dy,
            t: timestamp_ms,
        };
        self.history.push_back(current);
        if self.history.len() > HISTORY_SIZE {
            self.history.pop_front();
        }

        let dt = timestamp_ms - self.last_time;
        self.last_time = timestamp_ms;

        if self.history.len() < 2 || dt <= 0.0 {
            return vec![(dx, dy)];
        }

        // Calculate velocity vector
        let last = self.history.back().unwrap();
        let prev = self.history.get(self.history.len().saturating_sub(2)).unwrap_or(last);
        let vx = (last.x - prev.x) / dt.max(1.0);
        let vy = (last.y - prev.y) / dt.max(1.0);

        // Predict next frame position
        let predicted_x = last.x + vx * PREDICT_FRAME_TIME_MS;
        let predicted_y = last.y + vy * PREDICT_FRAME_TIME_MS;

        // Smoothing
        let smooth_x = last.x * (1.0 - SMOOTH_FACTOR) + predicted_x * SMOOTH_FACTOR;
        let smooth_y = last.y * (1.0 - SMOOTH_FACTOR) + predicted_y * SMOOTH_FACTOR;

        // Distance check
        let distance = (smooth_x * smooth_x + smooth_y * smooth_y).sqrt();
        if distance < JITTER_THRESHOLD {
            return vec![];
        }

        // Interpolate intermediate points if movement is large
        let mut points = vec![];
        let total_dist = (raw_dx * raw_dx + raw_dy * raw_dy).sqrt();
        if total_dist > MAX_INTERP_DISTANCE {
            let num_points = (total_dist / MAX_INTERP_DISTANCE).ceil() as usize;
            for i in 1..=num_points {
                let t = i as f64 / num_points as f64;
                points.push((
                    (smooth_x * t) as i16,
                    (smooth_y * t) as i16,
                ));
            }
        } else {
            points.push((smooth_x as i16, smooth_y as i16));
        }

        points
    }

    pub fn reset(&mut self) {
        self.history.clear();
        self.last_time = 0.0;
    }
}

impl Default for MouseInterpolator {
    fn default() -> Self {
        Self::new()
    }
}
