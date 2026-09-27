//! Host-side input capture: reads local evdev keyboards/mice, translates
//! events to HID frames (§5.3) and forwards them to every connected device.
//!
//! While capturing, matched devices are EVIOCGRABbed so events do not leak
//! into the local desktop (Synergy-style Host behaviour).

use crate::device_manager::DeviceManagerClient;
use crate::hal::keymap_tables::linux_to_hid_keycode;
use crate::protocol_ffi;
use std::fs::{File, OpenOptions};
use std::io::Read;
use std::os::unix::io::AsRawFd;
use std::sync::atomic::{AtomicBool, AtomicU16, Ordering};
use std::sync::{Arc, Mutex};
use tokio::runtime::Handle;
use tracing::{debug, info, warn};

// ---------------------------------------------------------------------------
// evdev constants (linux/input-event-codes.h) and ioctl numbers
// ---------------------------------------------------------------------------

const EV_SYN: u16 = 0x00;
const EV_KEY: u16 = 0x01;
const EV_REL: u16 = 0x02;

const REL_X: u16 = 0x00;
const REL_Y: u16 = 0x01;
const REL_HWHEEL: u16 = 0x06;
const REL_WHEEL: u16 = 0x08;

const BTN_LEFT: u16 = 0x110;
const BTN_RIGHT: u16 = 0x111;
const BTN_MIDDLE: u16 = 0x112;
const BTN_SIDE: u16 = 0x113;
const BTN_EXTRA: u16 = 0x114;

// Modifier keycodes and their HID bitmask positions (§6.2).
const KEY_LEFTCTRL: u16 = 29;
const KEY_LEFTSHIFT: u16 = 42;
const KEY_LEFTALT: u16 = 56;
const KEY_LEFTMETA: u16 = 125;
const KEY_RIGHTCTRL: u16 = 97;
const KEY_RIGHTSHIFT: u16 = 54;
const KEY_RIGHTALT: u16 = 100;
const KEY_RIGHTMETA: u16 = 126;

fn modifier_bit(code: u16) -> Option<u8> {
    match code {
        KEY_LEFTCTRL => Some(0x01),
        KEY_LEFTSHIFT => Some(0x02),
        KEY_LEFTALT => Some(0x04),
        KEY_LEFTMETA => Some(0x08),
        KEY_RIGHTCTRL => Some(0x10),
        KEY_RIGHTSHIFT => Some(0x20),
        KEY_RIGHTALT => Some(0x40),
        KEY_RIGHTMETA => Some(0x80),
        _ => None,
    }
}

// _IOR('E', 0x20 + ev, len) — EVIOCGBIT; _IOW('E', 0x90, int) — EVIOCGRAB.
fn ior(etype: u8, nr: u8, size: usize) -> libc::c_ulong {
    (1u64 << 30) | ((size as u64) << 16) | ((etype as u64) << 8) | (nr as u64)
}

fn eviocgbit(ev: u8, len: usize) -> libc::c_ulong {
    ior(b'E', 0x20 + ev, len)
}

const EVIOCGRAB: libc::c_ulong = (1u64 << 30) | (4u64 << 16) | ((b'E' as u64) << 8) | 0x90;

const KEY_BITMAP_LEN: usize = (0x2FF / 8) + 1; // KEY_MAX bytes
const EV_BITMAP_LEN: usize = 8; // EV_MAX bytes

const REL_FLUSH_INTERVAL_MS: u64 = 5;
const SCROLL_STEP: i16 = 120;

// ---------------------------------------------------------------------------
// Frame types (§5.2) and payload builders come from protocol_ffi so the wire
// encoding has a single source of truth.
// ---------------------------------------------------------------------------

#[derive(Default)]
struct ModifierState {
    bits: u8,
}

impl ModifierState {
    fn apply(&mut self, code: u16, value: i32) -> bool {
        match modifier_bit(code) {
            Some(bit) => {
                if value == 0 {
                    self.bits &= !bit;
                } else {
                    self.bits |= bit;
                }
                true
            }
            None => false,
        }
    }
}

struct CapturedDevice {
    file: File,
    grab: bool,
}

/// Shared control surface for the capture thread.
pub struct InputCapture {
    running: Arc<AtomicBool>,
    thread: Mutex<Option<std::thread::JoinHandle<()>>>,
    grabbed_files: Mutex<Vec<File>>,
}

impl InputCapture {
    /// Enumerate local keyboard/mouse evdev nodes and start forwarding.
    /// Returns Err when no capturable device can be opened (permissions).
    pub fn start(client: DeviceManagerClient, runtime: Handle) -> crate::error::Result<Arc<Self>> {
        let devices = open_input_devices()?;
        if devices.is_empty() {
            return Err(crate::error::FlowGridError::hal(
                "no readable /dev/input/event* device (add user to the input group or install 99-flowgrid.rules)",
            ));
        }

        let running = Arc::new(AtomicBool::new(true));
        let running_clone = Arc::clone(&running);
        let grabbed: Vec<File> = devices
            .iter()
            .filter(|d| d.grab)
            .map(|d| d.file.try_clone().expect("file clone"))
            .collect();

        let thread = std::thread::Builder::new()
            .name("input-capture".into())
            .spawn(move || capture_loop(devices, client, runtime, running_clone))
            .map_err(|e| crate::error::FlowGridError::io(format!("spawn capture thread: {e}")))?;

        info!("input capture started");
        Ok(Arc::new(Self {
            running,
            thread: Mutex::new(Some(thread)),
            grabbed_files: Mutex::new(grabbed),
        }))
    }

    pub fn stop(&self) {
        self.running.store(false, Ordering::SeqCst);
        if let Ok(mut slot) = self.thread.lock() {
            if let Some(handle) = slot.take() {
                let _ = handle.join();
            }
        }
        // Release grabs even if the thread already exited without cleanup.
        if let Ok(files) = self.grabbed_files.lock() {
            for file in files.iter() {
                release_grab(file.as_raw_fd());
            }
        }
        info!("input capture stopped");
    }

    pub fn is_capturing(&self) -> bool {
        self.running.load(Ordering::SeqCst)
    }
}

fn release_grab(fd: std::os::unix::io::RawFd) {
    let zero: libc::c_int = 0;
    // SAFETY: EVIOCGRAB with a valid evdev fd and a local c_int argument.
    unsafe {
        libc::ioctl(fd, EVIOCGRAB as libc::c_ulong, &zero as *const libc::c_int);
    }
}

struct DetectedDevice {
    path: std::path::PathBuf,
    is_keyboard: bool,
    is_mouse: bool,
}

fn detect_input_devices() -> Vec<DetectedDevice> {
    let mut found = Vec::new();
    let Ok(entries) = std::fs::read_dir("/dev/input") else {
        return found;
    };
    for entry in entries.flatten() {
        let path = entry.path();
        let name = path.file_name().and_then(|n| n.to_str()).unwrap_or("");
        if !name.starts_with("event") {
            continue;
        }
        let Ok(file) = OpenOptions::new().read(true).open(&path) else {
            debug!("cannot open {} (permission?)", path.display());
            continue;
        };
        let fd = file.as_raw_fd();

        // SAFETY: EVIOCGBIT ioctls with buffers sized per the kernel ABI.
        let mut ev_bits = [0u8; EV_BITMAP_LEN];
        let mut key_bits = [0u8; KEY_BITMAP_LEN];
        let mut rel_bits = [0u8; 2];
        unsafe {
            if libc::ioctl(fd, eviocgbit(0, EV_BITMAP_LEN), ev_bits.as_mut_ptr()) < 0 {
                continue;
            }
            libc::ioctl(fd, eviocgbit(EV_KEY as u8, KEY_BITMAP_LEN), key_bits.as_mut_ptr());
            libc::ioctl(fd, eviocgbit(EV_REL as u8, 2), rel_bits.as_mut_ptr());
        }

        let has_ev_key = bit_set(&ev_bits, EV_KEY as usize);
        let has_ev_rel = bit_set(&ev_bits, EV_REL as usize);
        let has_letter = bit_set(&key_bits, 30) || bit_set(&key_bits, 16); // KEY_A / KEY_Q
        let has_mouse_rel = bit_set(&rel_bits, REL_X as usize) && bit_set(&rel_bits, REL_Y as usize);
        let has_mouse_btn = bit_set(&key_bits, BTN_LEFT as usize);

        let is_keyboard = has_ev_key && has_letter;
        let is_mouse = (has_ev_rel && has_mouse_rel) || (has_ev_key && has_mouse_btn);
        if is_keyboard || is_mouse {
            found.push(DetectedDevice {
                path,
                is_keyboard,
                is_mouse,
            });
        }
    }
    found
}

fn bit_set(bits: &[u8], index: usize) -> bool {
    bits.get(index / 8)
        .map(|byte| byte & (1 << (index % 8)) != 0)
        .unwrap_or(false)
}

struct OpenedDevice {
    file: File,
    grab: bool,
}

fn open_input_devices() -> crate::error::Result<Vec<OpenedDevice>> {
    let mut opened = Vec::new();
    for detected in detect_input_devices() {
        // Read-write so EVIOCGRAB always works; fall back to read-only.
        let file = OpenOptions::new()
            .read(true)
            .write(true)
            .open(&detected.path)
            .or_else(|_| OpenOptions::new().read(true).open(&detected.path))
            .map_err(|e| crate::error::FlowGridError::io(format!("open {}: {e}", detected.path.display())))?;

        let grab = detected.is_keyboard || detected.is_mouse;
        let mut opened_device = OpenedDevice { file, grab };
        if grab {
            grab_device(&mut opened_device);
        }
        opened.push(OpenedDevice {
            file: opened_device.file,
            grab: opened_device.grab,
        });
    }
    Ok(opened)
}

fn grab_device(device: &mut OpenedDevice) {
    let one: libc::c_int = 1;
    let fd = device.file.as_raw_fd();
    // SAFETY: EVIOCGRAB with a valid evdev fd and a local c_int argument.
    let rc = unsafe { libc::ioctl(fd, EVIOCGRAB as libc::c_ulong, &one as *const libc::c_int) };
    if rc < 0 {
        debug!("EVIOCGRAB failed on fd {fd} (still capturing without exclusivity)");
        device.grab = false;
    }
}

fn capture_loop(
    mut devices: Vec<OpenedDevice>,
    client: DeviceManagerClient,
    runtime: Handle,
    running: Arc<AtomicBool>,
) {
    // fds plus per-device pending state for the poll loop.
    let mut poll_fds: Vec<libc::pollfd> = devices
        .iter()
        .map(|d| libc::pollfd {
            fd: d.file.as_raw_fd(),
            events: libc::POLLIN,
            revents: 0,
        })
        .collect();

    let mut mods = ModifierState::default();
    let mut sequence: AtomicU16 = AtomicU16::new(0x1000);
    let mut pending_dx: i32 = 0;
    let mut pending_dy: i32 = 0;
    let mut last_flush = std::time::Instant::now();

    while running.load(Ordering::SeqCst) {
        // SAFETY: pollfds reference the open evdev fds held by `devices`.
        let ready = unsafe {
            libc::poll(
                poll_fds.as_mut_ptr(),
                poll_fds.len() as libc::nfds_t,
                REL_FLUSH_INTERVAL_MS as libc::c_int,
            )
        };
        if ready < 0 {
            if std::io::Error::last_os_error().kind() == std::io::ErrorKind::Interrupted {
                continue;
            }
            warn!("poll on evdev fds failed; stopping capture");
            break;
        }

        for (index, pfd) in poll_fds.iter().enumerate() {
            if pfd.revents & libc::POLLIN == 0 {
                continue;
            }
            let mut buf = [0u8; 24 * 32]; // 32 input_event structs per read
            match devices[index].file.read(&mut buf) {
                Ok(n) if n >= 24 => {
                    for chunk in buf[..n - (n % 24)].chunks_exact(24) {
                        let (event_type, code, value) = parse_input_event(chunk);
                        match event_type {
                            EV_KEY => {
                                flush_rel(
                                    &mut pending_dx,
                                    &mut pending_dy,
                                    &mut last_flush,
                                    &client,
                                    &runtime,
                                    &sequence,
                                );
                                handle_key(
                                    code,
                                    value,
                                    &mut mods,
                                    &client,
                                    &runtime,
                                    &sequence,
                                );
                            }
                            EV_REL => {
                                match code {
                                    REL_X => pending_dx += value as i32,
                                    REL_Y => pending_dy += value as i32,
                                    REL_WHEEL => send_scroll(
                                        value as i16 * SCROLL_STEP,
                                        0,
                                        &client,
                                        &runtime,
                                        &sequence,
                                    ),
                                    REL_HWHEEL => send_scroll(
                                        0,
                                        value as i16 * SCROLL_STEP,
                                        &client,
                                        &runtime,
                                        &sequence,
                                    ),
                                    _ => {}
                                }
                            }
                            _ => {}
                        }
                    }
                }
                Ok(_) => {}
                Err(e) if e.kind() == std::io::ErrorKind::WouldBlock => {}
                Err(e) => {
                    warn!("evdev read failed: {e}");
                    return;
                }
            }
        }

        if pending_dx != 0
            || pending_dy != 0
            || last_flush.elapsed().as_millis() >= REL_FLUSH_INTERVAL_MS as u128
        {
            flush_rel(
                &mut pending_dx,
                &mut pending_dy,
                &mut last_flush,
                &client,
                &runtime,
                &sequence,
            );
        }
    }

    for device in &devices {
        if device.grab {
            release_grab(device.file.as_raw_fd());
        }
    }
}

fn parse_input_event(chunk: &[u8]) -> (u16, u16, i32) {
    // struct input_event { timeval(16 on 64-bit), u16 type, u16 code, i32 value }
    let event_type = u16::from_le_bytes([chunk[16], chunk[17]]);
    let code = u16::from_le_bytes([chunk[18], chunk[19]]);
    let value = i32::from_le_bytes([chunk[20], chunk[21], chunk[22], chunk[23]]);
    (event_type, code, value)
}

fn handle_key(
    code: u16,
    value: i32,
    mods: &mut ModifierState,
    client: &DeviceManagerClient,
    runtime: &Handle,
    sequence: &AtomicU16,
) {
    if value == 2 {
        return; // auto-repeat: let the remote OS handle repeats
    }
    let pressed = value == 1;

    if let Some(bit) = modifier_bit(code) {
        if pressed {
            mods.bits |= bit;
        } else {
            mods.bits &= !bit;
        }
        // Modifiers travel as regular key frames with their HID usage so the
        // remote tracks press and release independently of other keys.
        if let Some(hid) = linux_to_hid_keycode(code) {
            let frame = if pressed {
                build_frame(0x01, &protocol_ffi::payload_keydown(hid, mods.bits), sequence)
            } else {
                build_frame(0x02, &protocol_ffi::payload_keyup(hid), sequence)
            };
            broadcast(frame, client, runtime);
        }
        return;
    }

    let Some(hid) = linux_to_hid_keycode(code) else {
        return; // unmapped key (consumer controls, LEDs, ...)
    };
    let frame = if pressed {
        build_frame(0x01, &protocol_ffi::payload_keydown(hid, mods.bits), sequence)
    } else {
        build_frame(0x02, &protocol_ffi::payload_keyup(hid), sequence)
    };
    broadcast(frame, client, runtime);
}

fn flush_rel(
    pending_dx: &mut i32,
    pending_dy: &mut i32,
    last_flush: &mut std::time::Instant,
    client: &DeviceManagerClient,
    runtime: &Handle,
    sequence: &AtomicU16,
) {
    let dx = *pending_dx;
    let dy = *pending_dy;
    *pending_dx = 0;
    *pending_dy = 0;
    *last_flush = std::time::Instant::now();
    if dx == 0 && dy == 0 {
        return;
    }
    let frame = build_frame(
        0x03,
        &protocol_ffi::payload_mousemove(dx.clamp(i16::MIN as i32, i16::MAX as i32) as i16,
                                         dy.clamp(i16::MIN as i32, i16::MAX as i32) as i16),
        sequence,
    );
    broadcast(frame, client, runtime);
}

fn send_scroll(
    dy: i16,
    dx: i16,
    client: &DeviceManagerClient,
    runtime: &Handle,
    sequence: &AtomicU16,
) {
    let frame = build_frame(0x05, &protocol_ffi::payload_scroll(dy, dx), sequence);
    broadcast(frame, client, runtime);
}

fn build_frame(frame_type: u8, payload: &crate::error::Result<Vec<u8>>, sequence: &AtomicU16) -> Option<Vec<u8>> {
    let payload = match payload {
        Ok(p) => p,
        Err(e) => {
            warn!("payload build failed: {e}");
            return None;
        }
    };
    let seq = sequence.fetch_add(1, Ordering::Relaxed);
    let timestamp = std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .map(|d| d.as_micros() as u64)
        .unwrap_or(0);
    // SAFETY: protocol_ffi encode functions copy inputs into malloc'd buffers
    // that are freed inside the same call (see protocol_ffi.rs).
    match protocol_ffi::encode_frame(frame_type, seq, timestamp, payload) {
        Ok(frame) => Some(frame),
        Err(e) => {
            warn!("frame encode failed: {e}");
            None
        }
    }
}

fn broadcast(frame: Option<Vec<u8>>, client: &DeviceManagerClient, runtime: &Handle) {
    let Some(frame) = frame else { return };
    let client = client.clone();
    runtime.spawn(async move {
        for device in client.list_devices().await {
            if device.connected
                && let Err(e) = client.send_frame(device.id.clone(), frame.clone()).await
            {
                debug!("forward frame to {} failed: {e}", device.id);
            }
        }
    });
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::hal::keymap_tables::{hid_to_linux_keycode, linux_to_hid_keycode};

    #[test]
    fn bidirectional_keymap_roundtrip() {
        // Spot-check the shared table in both directions.
        for (hid, linux) in [(0x04u16, 30u16), (0x06, 46), (0x27, 11), (0x28, 28),
                             (0x2C, 57), (0x3A, 59), (0x52, 103), (0xE0, 29), (0xE7, 126)] {
            assert_eq!(hid_to_linux_keycode(hid), Some(linux), "hid 0x{hid:02X}");
            assert_eq!(linux_to_hid_keycode(linux), Some(hid), "linux {linux}");
        }
    }

    #[test]
    fn modifier_detection() {
        let mut mods = ModifierState::default();
        assert!(mods.apply(KEY_LEFTCTRL, 1));
        assert_eq!(mods.bits, 0x01);
        assert!(mods.apply(KEY_LEFTSHIFT, 1));
        assert_eq!(mods.bits, 0x03);
        assert!(mods.apply(KEY_LEFTCTRL, 0));
        assert_eq!(mods.bits, 0x02);
        // Non-modifier keys must not be swallowed.
        assert!(!mods.apply(30, 1));
    }

    #[test]
    fn input_event_parse() {
        // 24-byte evdev event: 16-byte timeval + type/code/value (LE).
        let mut buf = [0u8; 24];
        buf[16] = 0x01; // EV_KEY
        buf[18] = 30;   // KEY_A
        buf[20] = 1;    // pressed
        assert_eq!(parse_input_event(&buf), (EV_KEY, 30, 1));
    }
}
