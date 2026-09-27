pub mod keymap_tables;
pub mod linux_input;
pub mod mouse_interpolator;

use crate::error::Result;

/// Platform-agnostic HID abstraction.
pub trait PlatformHal: Send + Sync {
    fn init(&mut self) -> Result<()>;
    fn inject_key_down(&self, key_code: u16, modifiers: u8) -> Result<()>;
    fn inject_key_up(&self, key_code: u16, modifiers: u8) -> Result<()>;
    fn inject_mouse_move(&self, dx: i16, dy: i16) -> Result<()>;
    fn inject_mouse_button(&self, button_id: u8, state: u8) -> Result<()>;
    fn inject_scroll(&self, delta_y: i16, delta_x: i16) -> Result<()>;
    fn shutdown(&mut self) -> Result<()>;
}
