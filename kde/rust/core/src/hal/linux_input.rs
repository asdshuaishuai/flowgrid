//! Linux uinput HID injection.
use crate::error::{FlowGridError, Result};
use crate::hal::PlatformHal;
use libc::{c_int, c_void, ioctl, write};
use std::fs::OpenOptions;
use std::os::unix::fs::OpenOptionsExt;
use std::os::unix::io::{AsRawFd, RawFd};
use tracing::{debug, info};

// uinput constants from linux/uinput.h
const UI_DEV_CREATE: c_int = 0x5501;
const UI_DEV_DESTROY: c_int = 0x5502;
const UI_SET_EVBIT: c_int = 0x40045564;
const UI_SET_KEYBIT: c_int = 0x40045565;
const UI_SET_RELBIT: c_int = 0x40045566;

// EV_ types
const EV_KEY: u16 = 0x01;
const EV_REL: u16 = 0x02;
const EV_SYN: u16 = 0x00;

// REL_ codes
const REL_X: u16 = 0x00;
const REL_Y: u16 = 0x01;
const REL_HWHEEL: u16 = 0x06;
const REL_WHEEL: u16 = 0x08;

// SYN_ codes
const SYN_REPORT: u16 = 0x00;

// HID Usage ID to Linux keycode mapping — full bidirectional table lives in
// keymap_tables.rs (shared with the Host-side capture module).
pub(crate) use crate::hal::keymap_tables::hid_to_linux_keycode;

#[repr(C)]
struct InputEvent {
    time: libc::timeval,
    type_: u16,
    code: u16,
    value: i32,
}

impl InputEvent {
    fn new(type_: u16, code: u16, value: i32) -> Self {
        Self {
            time: libc::timeval { tv_sec: 0, tv_usec: 0 },
            type_,
            code,
            value,
        }
    }

    fn syn() -> Self {
        Self::new(EV_SYN, SYN_REPORT, 0)
    }
}

// HID modifier bits → Linux keycodes (KEY_LEFTCTRL=29, KEY_LEFTSHIFT=42, KEY_LEFTALT=56, KEY_LEFTMETA=125, etc.)
fn modifier_bits_to_linux_keycodes(modifiers: u8) -> Vec<u16> {
    let mut codes = Vec::new();
    if modifiers & 0x01 != 0 { codes.push(29); }   // Left Ctrl
    if modifiers & 0x02 != 0 { codes.push(42); }   // Left Shift
    if modifiers & 0x04 != 0 { codes.push(56); }   // Left Alt
    if modifiers & 0x08 != 0 { codes.push(125); }  // Left GUI (Meta)
    if modifiers & 0x10 != 0 { codes.push(97); }   // Right Ctrl
    if modifiers & 0x20 != 0 { codes.push(54); }   // Right Shift
    if modifiers & 0x40 != 0 { codes.push(100); }  // Right Alt (AltGr)
    if modifiers & 0x80 != 0 { codes.push(126); }  // Right GUI
    codes
}

pub struct LinuxInputInjector {
    fd: Option<RawFd>,
}

impl Default for LinuxInputInjector {
    fn default() -> Self {
        Self::new()
    }
}

impl LinuxInputInjector {
    pub fn new() -> Self {
        Self { fd: None }
    }

    fn setup_uinput(&self, fd: RawFd) -> Result<()> {
        // Enable event types
        unsafe {
            if ioctl(fd, UI_SET_EVBIT as u64, EV_KEY as c_int) < 0 {
                return Err(FlowGridError::hal("UI_SET_EVBIT EV_KEY failed"));
            }
            if ioctl(fd, UI_SET_EVBIT as u64, EV_REL as c_int) < 0 {
                return Err(FlowGridError::hal("UI_SET_EVBIT EV_REL failed"));
            }
        }

        // Enable all keys (simplified: enable a range)
        for key in 0..=255u16 {
            unsafe {
                ioctl(fd, UI_SET_KEYBIT as u64, key as c_int);
            }
        }

        // Enable relative axes
        unsafe {
            ioctl(fd, UI_SET_RELBIT as u64, REL_X as c_int);
            ioctl(fd, UI_SET_RELBIT as u64, REL_Y as c_int);
            ioctl(fd, UI_SET_RELBIT as u64, REL_WHEEL as c_int);
            ioctl(fd, UI_SET_RELBIT as u64, REL_HWHEEL as c_int);
        }

        // Setup device info
        #[repr(C)]
        struct UinputUserDev {
            name: [u8; 80],
            id: UinputId,
            ff_effects_max: i32,
            absmax: [i32; 64],
            absmin: [i32; 64],
            absfuzz: [i32; 64],
            absflat: [i32; 64],
        }

        #[repr(C)]
        struct UinputId {
            bustype: u16,
            vendor: u16,
            product: u16,
            version: u16,
        }

        let mut udev: UinputUserDev = unsafe { std::mem::zeroed() };
        let name = b"FlowGrid Virtual Input\0";
        udev.name[..name.len()].copy_from_slice(name);
        udev.id = UinputId {
            bustype: 0x03, // BUS_USB
            vendor: 0x1234,
            product: 0x5678,
            version: 1,
        };

        let bytes = unsafe {
            std::slice::from_raw_parts(
                &udev as *const _ as *const u8,
                std::mem::size_of::<UinputUserDev>(),
            )
        };

        let written = unsafe { write(fd, bytes.as_ptr() as *const c_void, bytes.len()) };
        if written < 0 {
            return Err(FlowGridError::hal("Failed to write uinput_user_dev"));
        }

        // Create device
        if unsafe { ioctl(fd, UI_DEV_CREATE as u64, 0) } < 0 {
            return Err(FlowGridError::hal("UI_DEV_CREATE failed"));
        }

        info!("uinput device created successfully");
        Ok(())
    }

    fn write_event(&self, ev: &InputEvent) -> Result<()> {
        let fd = self.fd.ok_or_else(|| FlowGridError::hal("uinput not initialized"))?;
        let bytes = unsafe {
            std::slice::from_raw_parts(
                ev as *const _ as *const u8,
                std::mem::size_of::<InputEvent>(),
            )
        };
        let written = unsafe { write(fd, bytes.as_ptr() as *const c_void, bytes.len()) };
        if written < 0 {
            return Err(FlowGridError::hal("Failed to write input event"));
        }
        Ok(())
    }
}

impl PlatformHal for LinuxInputInjector {
    fn init(&mut self) -> Result<()> {
        let file = OpenOptions::new()
            .write(true)
            .custom_flags(libc::O_NONBLOCK)
            .open("/dev/uinput")
            .map_err(|e| {
                if e.kind() == std::io::ErrorKind::PermissionDenied {
                    FlowGridError::PermissionDenied(
                        "Cannot access /dev/uinput. Please install udev rules: sudo cp resources/99-flowgrid.rules /etc/udev/rules.d/ && sudo udevadm control --reload".to_string(),
                    )
                } else {
                    FlowGridError::io(format!("Open /dev/uinput: {e}"))
                }
            })?;

        let fd = file.as_raw_fd();
        self.setup_uinput(fd)?;
        self.fd = Some(fd);
        // SAFETY: file.as_raw_fd() extracted the raw fd. We must prevent File::drop
        // from closing it since we will use the fd directly via ioctl/write until
        // shutdown() calls UI_DEV_DESTROY. This is the standard pattern for passing
        // ownership of a file descriptor to a C API or long-lived structure.
        std::mem::forget(file);
        Ok(())
    }

    fn inject_key_down(&self, key_code: u16, modifiers: u8) -> Result<()> {
        let linux_code = hid_to_linux_keycode(key_code)
            .ok_or_else(|| FlowGridError::hal(format!("Unknown HID key code: 0x{key_code:04X}")))?;
        debug!("Inject key down: HID=0x{key_code:04X} -> Linux={linux_code} modifiers=0x{modifiers:02X}");
        // Press modifiers first
        for mod_code in modifier_bits_to_linux_keycodes(modifiers) {
            self.write_event(&InputEvent::new(EV_KEY, mod_code, 1))?;
        }
        self.write_event(&InputEvent::new(EV_KEY, linux_code, 1))?;
        self.write_event(&InputEvent::syn())?;
        Ok(())
    }

    fn inject_key_up(&self, key_code: u16, modifiers: u8) -> Result<()> {
        let linux_code = hid_to_linux_keycode(key_code)
            .ok_or_else(|| FlowGridError::hal(format!("Unknown HID key code: 0x{key_code:04X}")))?;
        debug!("Inject key up: HID=0x{key_code:04X} -> Linux={linux_code} modifiers=0x{modifiers:02X}");
        // Release key first
        self.write_event(&InputEvent::new(EV_KEY, linux_code, 0))?;
        // Release modifiers
        for mod_code in modifier_bits_to_linux_keycodes(modifiers) {
            self.write_event(&InputEvent::new(EV_KEY, mod_code, 0))?;
        }
        self.write_event(&InputEvent::syn())?;
        Ok(())
    }

    fn inject_mouse_move(&self, dx: i16, dy: i16) -> Result<()> {
        if dx != 0 {
            self.write_event(&InputEvent::new(EV_REL, REL_X, dx as i32))?;
        }
        if dy != 0 {
            self.write_event(&InputEvent::new(EV_REL, REL_Y, dy as i32))?;
        }
        if dx != 0 || dy != 0 {
            self.write_event(&InputEvent::syn())?;
        }
        Ok(())
    }

    fn inject_mouse_button(&self, button_id: u8, state: u8) -> Result<()> {
        let linux_btn = match button_id {
            0x01 => 0x110, // BTN_LEFT
            0x02 => 0x111, // BTN_RIGHT
            0x03 => 0x112, // BTN_MIDDLE
            0x04 => 0x113, // BTN_SIDE
            0x05 => 0x114, // BTN_EXTRA
            _ => return Err(FlowGridError::hal(format!("Unknown mouse button: {button_id}"))),
        };
        self.write_event(&InputEvent::new(EV_KEY, linux_btn, state as i32))?;
        self.write_event(&InputEvent::syn())?;
        Ok(())
    }

    fn inject_scroll(&self, delta_y: i16, delta_x: i16) -> Result<()> {
        if delta_y != 0 {
            self.write_event(&InputEvent::new(EV_REL, REL_WHEEL, delta_y as i32))?;
        }
        if delta_x != 0 {
            self.write_event(&InputEvent::new(EV_REL, REL_HWHEEL, delta_x as i32))?;
        }
        if delta_y != 0 || delta_x != 0 {
            self.write_event(&InputEvent::syn())?;
        }
        Ok(())
    }

    fn shutdown(&mut self) -> Result<()> {
        if let Some(fd) = self.fd.take() {
            unsafe {
                ioctl(fd, UI_DEV_DESTROY as u64, 0);
            }
            info!("uinput device destroyed");
        }
        Ok(())
    }
}

impl Drop for LinuxInputInjector {
    fn drop(&mut self) {
        let _ = self.shutdown();
    }
}
