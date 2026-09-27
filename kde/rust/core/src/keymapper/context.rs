//! App context detection: active window, process name.
//! Uses `xdotool` or `qdbus` as fallback on Linux/KDE.

use crate::error::{FlowGridError, Result};
use std::process::Command;
use tracing::{debug, trace};

/// Returns the active window class (e.g., `"code-oss"`, `"firefox"`, `"com.microsoft.VSCode"`).
/// Falls back to `"global"` if detection fails.
pub fn get_active_window_class() -> String {
    if let Ok(class) = get_active_window_class_xdotool() {
        trace!("Active window class (xdotool): {class}");
        return class;
    }
    if let Ok(class) = get_active_window_class_qdbus() {
        trace!("Active window class (qdbus): {class}");
        return class;
    }
    "global".to_string()
}

fn get_active_window_class_xdotool() -> Result<String> {
    let output = Command::new("xdotool")
        .args(["getactivewindow", "getwindowclassname"])
        .output()
        .map_err(|e| FlowGridError::hal(format!("xdotool failed: {e}")))?;

    if !output.status.success() {
        return Err(FlowGridError::hal("xdotool returned non-zero"));
    }

    let class = String::from_utf8_lossy(&output.stdout)
        .trim()
        .to_string();

    if class.is_empty() {
        return Err(FlowGridError::hal("Empty xdotool output"));
    }

    Ok(class)
}

fn get_active_window_class_qdbus() -> Result<String> {
    // Try KWindowSystem via qdbus / org.kde.kwin
    let output = Command::new("qdbus")
        .args([
            "org.kde.KWin",
            "/KWin",
            "org.kde.KWin.activeWindow",
        ])
        .output()
        .map_err(|e| FlowGridError::hal(format!("qdbus failed: {e}")))?;

    if !output.status.success() {
        return Err(FlowGridError::hal("qdbus returned non-zero"));
    }

    let win_id = String::from_utf8_lossy(&output.stdout)
        .trim()
        .to_string();

    if win_id.is_empty() || win_id == "0" {
        return Err(FlowGridError::hal("No active window from qdbus"));
    }

    // Query window class for the given window id
    let output2 = Command::new("qdbus")
        .args([
            "org.kde.KWin",
            &win_id,
            "org.kde.KWin.Window.windowClass",
        ])
        .output()
        .map_err(|e| FlowGridError::hal(format!("qdbus windowClass failed: {e}")))?;

    if !output2.status.success() {
        return Err(FlowGridError::hal("qdbus windowClass returned non-zero"));
    }

    let class = String::from_utf8_lossy(&output2.stdout)
        .trim()
        .to_string();

    if class.is_empty() {
        return Err(FlowGridError::hal("Empty qdbus windowClass"));
    }

    Ok(class)
}

/// Returns the active window title as a convenience helper.
pub fn get_active_window_title() -> String {
    match Command::new("xdotool")
        .args(["getactivewindow", "getwindowtitle"])
        .output()
    {
        Ok(output) if output.status.success() => {
            String::from_utf8_lossy(&output.stdout).trim().to_string()
        }
        _ => {
            debug!("Failed to get active window title via xdotool");
            String::new()
        }
    }
}

/// Returns the active window PID as a convenience helper.
pub fn get_active_window_pid() -> Option<u32> {
    match Command::new("xdotool")
        .args(["getactivewindow", "getwindowpid"])
        .output()
    {
        Ok(output) if output.status.success() => {
            String::from_utf8_lossy(&output.stdout)
                .trim()
                .parse::<u32>()
                .ok()
        }
        _ => {
            debug!("Failed to get active window PID via xdotool");
            None
        }
    }
}

/// Returns the process name for the given PID.
pub fn get_process_name(pid: u32) -> Option<String> {
    match Command::new("ps")
        .args(["-p", &pid.to_string(), "-o", "comm="])
        .output()
    {
        Ok(output) if output.status.success() => {
            let name = String::from_utf8_lossy(&output.stdout).trim().to_string();
            if name.is_empty() {
                None
            } else {
                Some(name)
            }
        }
        _ => None,
    }
}
