//! Clipboard synchronization (§5.3 CLIPBOARD frames, text only in v1).
//!
//! Watches the local X11 CLIPBOARD selection; on change the new text is
//! broadcast to every connected device. Incoming CLIPBOARD text frames are
//! written back to the local selection and remembered so they are not echoed
//! back to the remote (loop suppression).

use crate::device_manager::DeviceManagerClient;
use crate::protocol_ffi;
use std::collections::hash_map::DefaultHasher;
use std::hash::{Hash, Hasher};
use std::sync::atomic::{AtomicBool, AtomicU16, Ordering};
use std::sync::{Arc, Mutex, OnceLock};
use tokio::runtime::Handle;
use tracing::{debug, info, warn};

/// Wire limit for one CLIPBOARD payload (§5.3: 64 KiB, larger needs §7.3).
const MAX_CLIPBOARD_BYTES: usize = 65536;
const POLL_INTERVAL: std::time::Duration = std::time::Duration::from_millis(1000);
pub const MIME_TEXT_UTF8: &str = "text/plain;charset=utf-8";
const FRAME_CLIPBOARD: u8 = 0x07;

fn hash_text(text: &str) -> u64 {
    let mut hasher = DefaultHasher::new();
    text.hash(&mut hasher);
    hasher.finish()
}

/// Shared state: hashes of the text we last pushed out / pulled in, used to
/// break the local-change → remote-frame → local-change loop.
struct ClipboardState {
    last_local_hash: u64,
    last_remote_hash: u64,
}

static STATE: OnceLock<Mutex<ClipboardState>> = OnceLock::new();
static RUNNING: AtomicBool = AtomicBool::new(false);
static SEQUENCE: AtomicU16 = AtomicU16::new(0x4000);

fn state() -> &'static Mutex<ClipboardState> {
    STATE.get_or_init(|| {
        Mutex::new(ClipboardState {
            last_local_hash: 0,
            last_remote_hash: 0,
        })
    })
}

fn next_sequence() -> u16 {
    SEQUENCE.fetch_add(1, Ordering::Relaxed)
}

/// Called by device_manager when a CLIPBOARD text frame arrives from a peer.
pub fn handle_incoming_text(text: &str) {
    let hash = hash_text(text);
    {
        let mut st = state().lock().unwrap();
        if st.last_remote_hash == hash {
            debug!("clipboard: duplicate remote text ignored");
            return;
        }
        st.last_remote_hash = hash;
    }
    let Ok(clipboard) = x11_clipboard::Clipboard::new() else {
        warn!("clipboard: cannot connect to X11 to store remote text");
        return;
    };
    if clipboard
        .store(clipboard.setter.atoms.clipboard, clipboard.setter.atoms.utf8_string, text.as_bytes().to_vec())
        .is_err()
    {
        warn!("clipboard: X11 store failed");
        return;
    }
    debug!("clipboard: stored {} bytes from remote", text.len());
}

pub struct ClipboardSync {
    stop: Arc<AtomicBool>,
    thread: Option<std::thread::JoinHandle<()>>,
}

impl ClipboardSync {
    /// Start watching the local clipboard and broadcasting changes.
    pub fn start(client: DeviceManagerClient, runtime: Handle) -> crate::error::Result<Self> {
        if RUNNING.swap(true, Ordering::SeqCst) {
            return Ok(Self {
                stop: Arc::new(AtomicBool::new(false)),
                thread: None,
            }); // already running; the caller's handle stays a no-op token
        }
        let stop = Arc::new(AtomicBool::new(false));
        let stop_clone = Arc::clone(&stop);
        let thread = std::thread::Builder::new()
            .name("clipboard-sync".into())
            .spawn(move || watch_loop(client, runtime, stop_clone))
            .map_err(|e| crate::error::FlowGridError::io(format!("spawn clipboard thread: {e}")))?;
        info!("clipboard sync started");
        Ok(Self {
            stop,
            thread: Some(thread),
        })
    }

    pub fn stop(mut self) {
        self.stop.store(false, Ordering::SeqCst);
        if let Some(handle) = self.thread.take() {
            let _ = handle.join();
        }
        RUNNING.store(false, Ordering::SeqCst);
        info!("clipboard sync stopped");
    }
}

fn watch_loop(client: DeviceManagerClient, runtime: Handle, stop: Arc<AtomicBool>) {
    let Ok(clipboard) = x11_clipboard::Clipboard::new() else {
        warn!("clipboard sync disabled: cannot connect to X11 display");
        return;
    };
    while !stop.load(Ordering::SeqCst) {
        std::thread::sleep(POLL_INTERVAL);
        let Ok(bytes) = clipboard.load(
            clipboard.getter.atoms.clipboard,
            clipboard.getter.atoms.utf8_string,
            clipboard.getter.atoms.property,
            Some(POLL_INTERVAL),
        ) else {
            continue; // selection not owned / transient failure
        };
        if bytes.is_empty() || bytes.len() > MAX_CLIPBOARD_BYTES {
            continue;
        }
        let Ok(text) = String::from_utf8(bytes) else {
            continue; // v1 only syncs UTF-8 text
        };
        let hash = hash_text(&text);
        {
            let mut st = state().lock().unwrap();
            if hash == st.last_local_hash || hash == st.last_remote_hash {
                continue; // unchanged, or text we just received from remote
            }
            st.last_local_hash = hash;
        }
        let payload = match protocol_ffi::payload_clipboard(MIME_TEXT_UTF8, text.as_bytes()) {
            Ok(p) => p,
            Err(e) => {
                debug!("clipboard payload build failed: {e}");
                continue;
            }
        };
        let timestamp = std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .map(|d| d.as_micros() as u64)
            .unwrap_or(0);
        let frame = match protocol_ffi::encode_frame(FRAME_CLIPBOARD, next_sequence(), timestamp, &payload)
        {
            Ok(f) => f,
            Err(e) => {
                debug!("clipboard frame encode failed: {e}");
                continue;
            }
        };
        debug!("clipboard: broadcasting {} bytes", text.len());
        let client = client.clone();
        runtime.spawn(async move {
            for device in client.list_devices().await {
                if device.connected
                    && let Err(e) = client.send_frame(device.id.clone(), frame.clone()).await
                {
                    debug!("clipboard send to {} failed: {e}", device.id);
                }
            }
        });
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn hash_is_stable_and_discriminating() {
        assert_eq!(hash_text("hello"), hash_text("hello"));
        assert_ne!(hash_text("hello"), hash_text("hello "));
        assert_ne!(hash_text(""), hash_text("x"));
    }

    #[test]
    fn mime_constant_matches_protocol() {
        assert_eq!(MIME_TEXT_UTF8, "text/plain;charset=utf-8");
    }
}
