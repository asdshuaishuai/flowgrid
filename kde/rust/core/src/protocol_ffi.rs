#![allow(clippy::too_many_arguments)]

//! Safe Rust wrappers around the Go protocol cgo FFI.

use crate::error::{FlowGridError, Result};
use std::ffi::{c_char, c_double, c_int, c_void, CStr, CString};

#[link(name = "flowgrid_protocol", kind = "static")]
#[allow(dead_code)]
#[allow(clippy::too_many_arguments)]
unsafe extern "C" {
    fn fg_frame_encode(
        frame_type: u8,
        sequence: u16,
        timestamp: u64,
        payload: *const u8,
        payload_len: u8,
        out_len: *mut usize,
    ) -> *mut u8;

    fn fg_frame_decode(
        data: *const u8,
        len: usize,
        out_type: *mut u8,
        out_seq: *mut u16,
        out_ts: *mut u64,
        out_payload_len: *mut u8,
        out_payload: *mut *mut u8,
    ) -> c_int;

    fn fg_frame_encode_with_hmac(
        frame_type: u8,
        sequence: u16,
        timestamp: u64,
        payload: *const u8,
        payload_len: u8,
        hmac_key: *const u8,
        hmac_key_len: usize,
        out_len: *mut usize,
    ) -> *mut u8;

    fn fg_frame_decode_with_hmac(
        data: *const u8,
        len: usize,
        hmac_key: *const u8,
        hmac_key_len: usize,
        out_type: *mut u8,
        out_seq: *mut u16,
        out_ts: *mut u64,
        out_payload_len: *mut u8,
        out_payload: *mut *mut u8,
    ) -> c_int;

    fn fg_payload_keydown(key_code: u16, modifiers: u8, out_len: *mut usize) -> *mut u8;
    fn fg_payload_keyup(key_code: u16, out_len: *mut usize) -> *mut u8;
    fn fg_payload_mousemove(delta_x: i16, delta_y: i16, out_len: *mut usize) -> *mut u8;
    fn fg_payload_mousebtn(button_id: u8, state: u8, out_len: *mut usize) -> *mut u8;
    fn fg_payload_scroll(delta_y: i16, delta_x: i16, out_len: *mut usize) -> *mut u8;
    fn fg_payload_latency_ping(timestamp: u64, out_len: *mut usize) -> *mut u8;
    fn fg_payload_latency_pong(timestamp: u64, out_len: *mut usize) -> *mut u8;
    fn fg_payload_disconnect(reason: u8, out_len: *mut usize) -> *mut u8;
    fn fg_payload_error(code: u8, msg: *const c_char, out_len: *mut usize) -> *mut u8;

    fn fg_payload_unmarshal_keydown(
        data: *const u8,
        len: usize,
        out_key_code: *mut u16,
        out_modifiers: *mut u8,
    ) -> c_int;
    fn fg_payload_unmarshal_keyup(data: *const u8, len: usize, out_key_code: *mut u16) -> c_int;
    fn fg_payload_unmarshal_mousemove(
        data: *const u8,
        len: usize,
        out_dx: *mut i16,
        out_dy: *mut i16,
    ) -> c_int;
    fn fg_payload_unmarshal_mousebtn(
        data: *const u8,
        len: usize,
        out_btn: *mut u8,
        out_state: *mut u8,
    ) -> c_int;
    fn fg_payload_unmarshal_scroll(
        data: *const u8,
        len: usize,
        out_dy: *mut i16,
        out_dx: *mut i16,
    ) -> c_int;
    fn fg_payload_unmarshal_disconnect(data: *const u8, len: usize, out_reason: *mut u8) -> c_int;
    fn fg_payload_clipboard(
        mime: *const c_char,
        data: *const u8,
        data_len: usize,
        out_len: *mut usize,
    ) -> *mut u8;
    fn fg_payload_unmarshal_clipboard(
        data: *const u8,
        len: usize,
        out_mime_len: *mut u8,
        out_mime: *mut *mut u8,
        out_data_len: *mut usize,
        out_data: *mut *mut u8,
    ) -> c_int;

    fn fg_remap_modifiers(mods: u8, from_platform: u8, to_platform: u8) -> u8;

    fn fg_latency_monitor_new(max_samples: c_int) -> usize;
    fn fg_latency_record_ping(id: usize, send_time_us: u64);
    fn fg_latency_record_pong(
        id: usize,
        pong_timestamp_us: u64,
        now_us: u64,
        out_ms: *mut c_double,
        out_jitter: *mut c_double,
    ) -> c_int;
    fn fg_latency_latest(id: usize, out_ms: *mut c_double, out_jitter: *mut c_double, out_quality: *mut c_int) -> c_int;
    fn fg_latency_stats(id: usize, out_min: *mut c_double, out_max: *mut c_double, out_avg: *mut c_double) -> c_int;
    fn fg_latency_monitor_free(id: usize);

    fn fg_heartbeat_tracker_new() -> usize;
    fn fg_heartbeat_received(id: usize);
    fn fg_heartbeat_tick(id: usize) -> c_int;
    fn fg_heartbeat_tracker_free(id: usize);

    fn fg_reconnect_state_new() -> usize;
    fn fg_reconnect_start(id: usize, out_backoff_ms: *mut u64, out_ok: *mut c_int);
    fn fg_reconnect_set_connected(id: usize);
    fn fg_reconnect_reset(id: usize);
    fn fg_reconnect_state_free(id: usize);

    fn fg_sequence_tracker_new(seq_init: u32) -> usize;
    fn fg_sequence_next(id: usize) -> u16;
    fn fg_sequence_check(id: usize, received: u16) -> c_int;
    fn fg_sequence_reset(id: usize, seq_init: u32);
    fn fg_sequence_tracker_free(id: usize);

    fn fg_fragment_reassembler_new() -> usize;
    fn fg_fragment_feed(
        id: usize,
        data: *const u8,
        len: usize,
        out_len: *mut usize,
        out_data: *mut *mut u8,
    ) -> c_int;
    fn fg_fragment_cleanup(id: usize);
    fn fg_fragment_reassembler_free(id: usize);

    fn fg_error_string(code: u8) -> *mut c_char;

    fn fg_identify_payload_encode(
        version: u8,
        platform: u8,
        role: u8,
        caps: u8,
        seq_init: u32,
        name: *const c_char,
        out_len: *mut usize,
    ) -> *mut u8;
    fn fg_identify_response_encode(
        status: u8,
        platform: u8,
        caps: u8,
        name: *const c_char,
        out_len: *mut usize,
    ) -> *mut u8;

    fn fg_ble_advertising_encode(
        protocol_ver: u8,
        platform: u8,
        caps: u8,
        wifi_chan: u16,
        out_len: *mut usize,
    ) -> *mut u8;
    fn fg_ble_advertising_decode(
        data: *const u8,
        len: usize,
        out_ver: *mut u8,
        out_platform: *mut u8,
        out_caps: *mut u8,
        out_chan: *mut u16,
    ) -> c_int;
    fn fg_classify_rssi(rssi: c_int) -> c_int;

    fn fg_wifidirect_request_encode(
        version: u8,
        port: u16,
        ip_addr: *const u8,
        subnet: *const u8,
        channel: u8,
        band: u8,
        security: u8,
        psk_hash: *const u8,
        out_len: *mut usize,
    ) -> *mut u8;
    fn fg_wifidirect_response_encode(
        version: u8,
        status: u8,
        port: u16,
        channel: u16,
        band: u8,
        out_len: *mut usize,
    ) -> *mut u8;

    fn fg_free_buffer(buf: *mut c_void);
    fn fg_keymap_free_string(s: *mut c_char);
}

pub fn encode_frame(
    frame_type: u8,
    sequence: u16,
    timestamp: u64,
    payload: &[u8],
) -> Result<Vec<u8>> {
    if payload.len() > u8::MAX as usize {
        return Err(FlowGridError::ProtocolFfi(format!(
            "payload length {} exceeds max {}",
            payload.len(),
            u8::MAX
        )));
    }
    let mut out_len: usize = 0;
    // SAFETY: fg_frame_encode copies from payload.as_ptr() for payload.len() bytes
    // and returns a malloc'd buffer. We free it below with fg_free_buffer.
    let buf = unsafe {
        fg_frame_encode(
            frame_type,
            sequence,
            timestamp,
            payload.as_ptr(),
            payload.len() as u8,
            &mut out_len,
        )
    };
    if buf.is_null() || out_len == 0 {
        return Err(FlowGridError::ProtocolFfi("encode_frame failed".into()));
    }
    // SAFETY: buf is valid for out_len bytes per fg_frame_encode contract.
    let result = unsafe { std::slice::from_raw_parts(buf, out_len).to_vec() };
    // SAFETY: buf was allocated by fg_frame_encode via C.malloc; fg_free_buffer uses C.free.
    unsafe { fg_free_buffer(buf as *mut c_void) };
    Ok(result)
}

pub fn decode_frame(data: &[u8]) -> Result<(u8, u16, u64, Vec<u8>)> {
    let mut out_type: u8 = 0;
    let mut out_seq: u16 = 0;
    let mut out_ts: u64 = 0;
    let mut out_payload_len: u8 = 0;
    let mut out_payload: *mut u8 = std::ptr::null_mut();
    let rc = unsafe {
        fg_frame_decode(
            data.as_ptr(),
            data.len(),
            &mut out_type,
            &mut out_seq,
            &mut out_ts,
            &mut out_payload_len,
            &mut out_payload,
        )
    };
    if rc != 0 {
        return Err(FlowGridError::ProtocolFfi("decode_frame failed".into()));
    }
    let payload = if out_payload_len > 0 && !out_payload.is_null() {
        let p = unsafe { std::slice::from_raw_parts(out_payload, out_payload_len as usize).to_vec() };
        unsafe { fg_free_buffer(out_payload as *mut c_void) };
        p
    } else {
        Vec::new()
    };
    Ok((out_type, out_seq, out_ts, payload))
}

pub fn encode_frame_with_hmac(
    frame_type: u8,
    sequence: u16,
    timestamp: u64,
    payload: &[u8],
    hmac_key: &[u8],
) -> Result<Vec<u8>> {
    if payload.len() > u8::MAX as usize {
        return Err(FlowGridError::ProtocolFfi(format!(
            "payload length {} exceeds max {}",
            payload.len(),
            u8::MAX
        )));
    }
    let mut out_len: usize = 0;
    // SAFETY: fg_frame_encode_with_hmac copies from payload.as_ptr() and hmac_key.as_ptr()
    // and returns a malloc'd buffer. We free it below with fg_free_buffer.
    let buf = unsafe {
        fg_frame_encode_with_hmac(
            frame_type,
            sequence,
            timestamp,
            payload.as_ptr(),
            payload.len() as u8,
            hmac_key.as_ptr(),
            hmac_key.len(),
            &mut out_len,
        )
    };
    if buf.is_null() || out_len == 0 {
        return Err(FlowGridError::ProtocolFfi("encode_frame_with_hmac failed".into()));
    }
    // SAFETY: buf is valid for out_len bytes per fg_frame_encode_with_hmac contract.
    let result = unsafe { std::slice::from_raw_parts(buf, out_len).to_vec() };
    // SAFETY: buf was allocated by fg_frame_encode_with_hmac via C.malloc; fg_free_buffer uses C.free.
    unsafe { fg_free_buffer(buf as *mut c_void) };
    Ok(result)
}

pub fn decode_frame_with_hmac(data: &[u8], hmac_key: &[u8]) -> Result<(u8, u16, u64, Vec<u8>)> {
    let mut out_type: u8 = 0;
    let mut out_seq: u16 = 0;
    let mut out_ts: u64 = 0;
    let mut out_payload_len: u8 = 0;
    let mut out_payload: *mut u8 = std::ptr::null_mut();
    let rc = unsafe {
        fg_frame_decode_with_hmac(
            data.as_ptr(),
            data.len(),
            hmac_key.as_ptr(),
            hmac_key.len(),
            &mut out_type,
            &mut out_seq,
            &mut out_ts,
            &mut out_payload_len,
            &mut out_payload,
        )
    };
    if rc != 0 {
        return Err(FlowGridError::ProtocolFfi("decode_frame_with_hmac failed (HMAC mismatch or bad frame)".into()));
    }
    let payload = if out_payload_len > 0 && !out_payload.is_null() {
        let p = unsafe { std::slice::from_raw_parts(out_payload, out_payload_len as usize).to_vec() };
        unsafe { fg_free_buffer(out_payload as *mut c_void) };
        p
    } else {
        Vec::new()
    };
    Ok((out_type, out_seq, out_ts, payload))
}

pub fn payload_keydown(key_code: u16, modifiers: u8) -> Result<Vec<u8>> {
    let mut out_len: usize = 0;
    let buf = unsafe { fg_payload_keydown(key_code, modifiers, &mut out_len) };
    if buf.is_null() {
        return Err(FlowGridError::ProtocolFfi("payload_keydown failed".into()));
    }
    let result = unsafe { std::slice::from_raw_parts(buf, out_len).to_vec() };
    unsafe { fg_free_buffer(buf as *mut c_void) };
    Ok(result)
}

pub fn payload_keyup(key_code: u16) -> Result<Vec<u8>> {
    let mut out_len: usize = 0;
    let buf = unsafe { fg_payload_keyup(key_code, &mut out_len) };
    if buf.is_null() {
        return Err(FlowGridError::ProtocolFfi("payload_keyup failed".into()));
    }
    let result = unsafe { std::slice::from_raw_parts(buf, out_len).to_vec() };
    unsafe { fg_free_buffer(buf as *mut c_void) };
    Ok(result)
}

pub fn payload_mousemove(delta_x: i16, delta_y: i16) -> Result<Vec<u8>> {
    let mut out_len: usize = 0;
    let buf = unsafe { fg_payload_mousemove(delta_x, delta_y, &mut out_len) };
    if buf.is_null() {
        return Err(FlowGridError::ProtocolFfi("payload_mousemove failed".into()));
    }
    let result = unsafe { std::slice::from_raw_parts(buf, out_len).to_vec() };
    unsafe { fg_free_buffer(buf as *mut c_void) };
    Ok(result)
}

pub fn payload_mousebtn(button_id: u8, state: u8) -> Result<Vec<u8>> {
    let mut out_len: usize = 0;
    let buf = unsafe { fg_payload_mousebtn(button_id, state, &mut out_len) };
    if buf.is_null() {
        return Err(FlowGridError::ProtocolFfi("payload_mousebtn failed".into()));
    }
    let result = unsafe { std::slice::from_raw_parts(buf, out_len).to_vec() };
    unsafe { fg_free_buffer(buf as *mut c_void) };
    Ok(result)
}

pub fn payload_scroll(delta_y: i16, delta_x: i16) -> Result<Vec<u8>> {
    let mut out_len: usize = 0;
    let buf = unsafe { fg_payload_scroll(delta_y, delta_x, &mut out_len) };
    if buf.is_null() {
        return Err(FlowGridError::ProtocolFfi("payload_scroll failed".into()));
    }
    let result = unsafe { std::slice::from_raw_parts(buf, out_len).to_vec() };
    unsafe { fg_free_buffer(buf as *mut c_void) };
    Ok(result)
}

pub fn payload_clipboard(mime: &str, data: &[u8]) -> Result<Vec<u8>> {
    if data.len() > u8::MAX as usize * 256 {
        return Err(FlowGridError::ProtocolFfi(format!(
            "clipboard data {} exceeds 64 KiB wire limit",
            data.len()
        )));
    }
    let mime_c = std::ffi::CString::new(mime)
        .map_err(|_| FlowGridError::ProtocolFfi("clipboard mime contains NUL".into()))?;
    let mut out_len: usize = 0;
    // SAFETY: fg_payload_clipboard copies from mime_c/data and returns a
    // malloc'd buffer freed below with fg_free_buffer.
    let buf = unsafe {
        fg_payload_clipboard(mime_c.as_ptr(), data.as_ptr(), data.len(), &mut out_len)
    };
    if buf.is_null() {
        return Err(FlowGridError::ProtocolFfi("payload_clipboard failed".into()));
    }
    let result = unsafe { std::slice::from_raw_parts(buf, out_len).to_vec() };
    unsafe { fg_free_buffer(buf as *mut c_void) };
    Ok(result)
}

pub struct ClipboardContent {
    pub mime: String,
    pub data: Vec<u8>,
}

pub fn unmarshal_clipboard(data: &[u8]) -> Result<ClipboardContent> {
    let mut mime_len: u8 = 0;
    let mut mime_ptr: *mut u8 = std::ptr::null_mut();
    let mut data_len: usize = 0;
    let mut data_ptr: *mut u8 = std::ptr::null_mut();
    // SAFETY: fg_payload_unmarshal_clipboard fills the out params with malloc'd
    // buffers; both are freed below via fg_free_buffer.
    let rc = unsafe {
        fg_payload_unmarshal_clipboard(
            data.as_ptr(),
            data.len(),
            &mut mime_len,
            &mut mime_ptr,
            &mut data_len,
            &mut data_ptr,
        )
    };
    if rc != 0 {
        return Err(FlowGridError::ProtocolFfi("unmarshal_clipboard failed".into()));
    }
    let take = |ptr: *mut u8, n: usize| -> Vec<u8> {
        if ptr.is_null() || n == 0 {
            Vec::new()
        } else {
            // SAFETY: buffer holds exactly n bytes per the fg contract.
            unsafe { std::slice::from_raw_parts(ptr, n).to_vec() }
        }
    };
    let mime_bytes = take(mime_ptr, mime_len as usize);
    let payload = take(data_ptr, data_len);
    // SAFETY: both buffers were allocated by C.malloc; free them here.
    unsafe {
        if !mime_ptr.is_null() {
            fg_free_buffer(mime_ptr as *mut c_void);
        }
        if !data_ptr.is_null() {
            fg_free_buffer(data_ptr as *mut c_void);
        }
    }
    Ok(ClipboardContent {
        mime: String::from_utf8_lossy(&mime_bytes).into_owned(),
        data: payload,
    })
}

pub fn payload_latency_ping(timestamp: u64) -> Result<Vec<u8>> {
    let mut out_len: usize = 0;
    let buf = unsafe { fg_payload_latency_ping(timestamp, &mut out_len) };
    if buf.is_null() {
        return Err(FlowGridError::ProtocolFfi("payload_latency_ping failed".into()));
    }
    let result = unsafe { std::slice::from_raw_parts(buf, out_len).to_vec() };
    unsafe { fg_free_buffer(buf as *mut c_void) };
    Ok(result)
}

pub fn payload_latency_pong(timestamp: u64) -> Result<Vec<u8>> {
    let mut out_len: usize = 0;
    let buf = unsafe { fg_payload_latency_pong(timestamp, &mut out_len) };
    if buf.is_null() {
        return Err(FlowGridError::ProtocolFfi("payload_latency_pong failed".into()));
    }
    let result = unsafe { std::slice::from_raw_parts(buf, out_len).to_vec() };
    unsafe { fg_free_buffer(buf as *mut c_void) };
    Ok(result)
}

pub fn payload_disconnect(reason: u8) -> Result<Vec<u8>> {
    let mut out_len: usize = 0;
    let buf = unsafe { fg_payload_disconnect(reason, &mut out_len) };
    if buf.is_null() {
        return Err(FlowGridError::ProtocolFfi("payload_disconnect failed".into()));
    }
    let result = unsafe { std::slice::from_raw_parts(buf, out_len).to_vec() };
    unsafe { fg_free_buffer(buf as *mut c_void) };
    Ok(result)
}

pub fn payload_error(code: u8, message: &str) -> Result<Vec<u8>> {
    let c_msg = CString::new(message).map_err(|e| FlowGridError::ProtocolFfi(format!("{e}")))?;
    let mut out_len: usize = 0;
    let buf = unsafe { fg_payload_error(code, c_msg.as_ptr(), &mut out_len) };
    if buf.is_null() {
        return Err(FlowGridError::ProtocolFfi("payload_error failed".into()));
    }
    let result = unsafe { std::slice::from_raw_parts(buf, out_len).to_vec() };
    unsafe { fg_free_buffer(buf as *mut c_void) };
    Ok(result)
}

pub fn unmarshal_keydown(data: &[u8]) -> Result<(u16, u8)> {
    let mut key_code: u16 = 0;
    let mut modifiers: u8 = 0;
    let rc = unsafe { fg_payload_unmarshal_keydown(data.as_ptr(), data.len(), &mut key_code, &mut modifiers) };
    if rc != 0 {
        return Err(FlowGridError::ProtocolFfi("unmarshal_keydown failed".into()));
    }
    Ok((key_code, modifiers))
}

pub fn unmarshal_keyup(data: &[u8]) -> Result<u16> {
    let mut key_code: u16 = 0;
    let rc = unsafe { fg_payload_unmarshal_keyup(data.as_ptr(), data.len(), &mut key_code) };
    if rc != 0 {
        return Err(FlowGridError::ProtocolFfi("unmarshal_keyup failed".into()));
    }
    Ok(key_code)
}

pub fn unmarshal_mousemove(data: &[u8]) -> Result<(i16, i16)> {
    let mut dx: i16 = 0;
    let mut dy: i16 = 0;
    let rc = unsafe { fg_payload_unmarshal_mousemove(data.as_ptr(), data.len(), &mut dx, &mut dy) };
    if rc != 0 {
        return Err(FlowGridError::ProtocolFfi("unmarshal_mousemove failed".into()));
    }
    Ok((dx, dy))
}

pub fn unmarshal_mousebtn(data: &[u8]) -> Result<(u8, u8)> {
    let mut btn: u8 = 0;
    let mut state: u8 = 0;
    let rc = unsafe { fg_payload_unmarshal_mousebtn(data.as_ptr(), data.len(), &mut btn, &mut state) };
    if rc != 0 {
        return Err(FlowGridError::ProtocolFfi("unmarshal_mousebtn failed".into()));
    }
    Ok((btn, state))
}

pub fn unmarshal_scroll(data: &[u8]) -> Result<(i16, i16)> {
    let mut dy: i16 = 0;
    let mut dx: i16 = 0;
    let rc = unsafe { fg_payload_unmarshal_scroll(data.as_ptr(), data.len(), &mut dy, &mut dx) };
    if rc != 0 {
        return Err(FlowGridError::ProtocolFfi("unmarshal_scroll failed".into()));
    }
    Ok((dy, dx))
}

pub fn unmarshal_disconnect(data: &[u8]) -> Result<u8> {
    let mut reason: u8 = 0;
    let rc = unsafe { fg_payload_unmarshal_disconnect(data.as_ptr(), data.len(), &mut reason) };
    if rc != 0 {
        return Err(FlowGridError::ProtocolFfi("unmarshal_disconnect failed".into()));
    }
    Ok(reason)
}

pub fn remap_modifiers(mods: u8, from_platform: u8, to_platform: u8) -> u8 {
    unsafe { fg_remap_modifiers(mods, from_platform, to_platform) }
}

pub fn error_string(code: u8) -> String {
    unsafe {
        let ptr = fg_error_string(code);
        if ptr.is_null() {
            return format!("error 0x{code:02X}");
        }
        let s = CStr::from_ptr(ptr).to_string_lossy().into_owned();
        fg_keymap_free_string(ptr);
        s
    }
}

pub fn identify_payload(
    version: u8,
    platform: u8,
    role: u8,
    caps: u8,
    seq_init: u32,
    name: &str,
) -> Result<Vec<u8>> {
    let c_name = CString::new(name).map_err(|e| FlowGridError::ProtocolFfi(format!("{e}")))?;
    let mut out_len: usize = 0;
    let buf = unsafe {
        fg_identify_payload_encode(version, platform, role, caps, seq_init, c_name.as_ptr(), &mut out_len)
    };
    if buf.is_null() {
        return Err(FlowGridError::ProtocolFfi("identify_payload failed".into()));
    }
    let result = unsafe { std::slice::from_raw_parts(buf, out_len).to_vec() };
    unsafe { fg_free_buffer(buf as *mut c_void) };
    Ok(result)
}

pub fn identify_response(status: u8, platform: u8, caps: u8, name: &str) -> Result<Vec<u8>> {
    let c_name = CString::new(name).map_err(|e| FlowGridError::ProtocolFfi(format!("{e}")))?;
    let mut out_len: usize = 0;
    let buf = unsafe {
        fg_identify_response_encode(status, platform, caps, c_name.as_ptr(), &mut out_len)
    };
    if buf.is_null() {
        return Err(FlowGridError::ProtocolFfi("identify_response failed".into()));
    }
    let result = unsafe { std::slice::from_raw_parts(buf, out_len).to_vec() };
    unsafe { fg_free_buffer(buf as *mut c_void) };
    Ok(result)
}

pub fn ble_advertising_encode(protocol_ver: u8, platform: u8, caps: u8, wifi_chan: u16) -> Result<Vec<u8>> {
    let mut out_len: usize = 0;
    let buf = unsafe { fg_ble_advertising_encode(protocol_ver, platform, caps, wifi_chan, &mut out_len) };
    if buf.is_null() {
        return Err(FlowGridError::ProtocolFfi("ble_advertising_encode failed".into()));
    }
    let result = unsafe { std::slice::from_raw_parts(buf, out_len).to_vec() };
    unsafe { fg_free_buffer(buf as *mut c_void) };
    Ok(result)
}

pub fn ble_advertising_decode(data: &[u8]) -> Result<(u8, u8, u8, u16)> {
    let mut ver: u8 = 0;
    let mut platform: u8 = 0;
    let mut caps: u8 = 0;
    let mut chan: u16 = 0;
    let rc = unsafe { fg_ble_advertising_decode(data.as_ptr(), data.len(), &mut ver, &mut platform, &mut caps, &mut chan) };
    if rc != 0 {
        return Err(FlowGridError::ProtocolFfi("ble_advertising_decode failed".into()));
    }
    Ok((ver, platform, caps, chan))
}

pub fn classify_rssi(rssi: i32) -> i32 {
    unsafe { fg_classify_rssi(rssi) }
}

pub fn wifidirect_request_encode(
    version: u8,
    port: u16,
    ip_addr: [u8; 4],
    subnet: [u8; 4],
    channel: u8,
    band: u8,
    security: u8,
    psk_hash: [u8; 4],
) -> Result<Vec<u8>> {
    let mut out_len: usize = 0;
    let buf = unsafe {
        fg_wifidirect_request_encode(
            version,
            port,
            ip_addr.as_ptr(),
            subnet.as_ptr(),
            channel,
            band,
            security,
            psk_hash.as_ptr(),
            &mut out_len,
        )
    };
    if buf.is_null() {
        return Err(FlowGridError::ProtocolFfi("wifidirect_request_encode failed".into()));
    }
    let result = unsafe { std::slice::from_raw_parts(buf, out_len).to_vec() };
    unsafe { fg_free_buffer(buf as *mut c_void) };
    Ok(result)
}

pub fn wifidirect_response_encode(version: u8, status: u8, port: u16, channel: u16, band: u8) -> Result<Vec<u8>> {
    let mut out_len: usize = 0;
    let buf = unsafe { fg_wifidirect_response_encode(version, status, port, channel, band, &mut out_len) };
    if buf.is_null() {
        return Err(FlowGridError::ProtocolFfi("wifidirect_response_encode failed".into()));
    }
    let result = unsafe { std::slice::from_raw_parts(buf, out_len).to_vec() };
    unsafe { fg_free_buffer(buf as *mut c_void) };
    Ok(result)
}

// --- LatencyMonitor wrapper ---

pub struct LatencyMonitor {
    id: usize,
}

impl LatencyMonitor {
    pub fn new(max_samples: i32) -> Self {
        let id = unsafe { fg_latency_monitor_new(max_samples) };
        Self { id }
    }

    pub fn record_ping(&self, send_time_us: u64) {
        unsafe { fg_latency_record_ping(self.id, send_time_us) };
    }

    pub fn record_pong(&self, pong_timestamp_us: u64, now_us: u64) -> Result<(f64, f64)> {
        let mut ms: f64 = 0.0;
        let mut jitter: f64 = 0.0;
        let rc = unsafe { fg_latency_record_pong(self.id, pong_timestamp_us, now_us, &mut ms, &mut jitter) };
        if rc != 0 {
            return Err(FlowGridError::ProtocolFfi("record_pong failed".into()));
        }
        Ok((ms, jitter))
    }

    pub fn latest(&self) -> Result<(f64, f64, i32)> {
        let mut ms: f64 = 0.0;
        let mut jitter: f64 = 0.0;
        let mut quality: i32 = 0;
        let rc = unsafe { fg_latency_latest(self.id, &mut ms, &mut jitter, &mut quality) };
        if rc != 0 {
            return Err(FlowGridError::ProtocolFfi("latest failed".into()));
        }
        Ok((ms, jitter, quality))
    }

    pub fn stats(&self) -> Result<(f64, f64, f64)> {
        let mut min: f64 = 0.0;
        let mut max: f64 = 0.0;
        let mut avg: f64 = 0.0;
        let rc = unsafe { fg_latency_stats(self.id, &mut min, &mut max, &mut avg) };
        if rc != 0 {
            return Err(FlowGridError::ProtocolFfi("stats failed".into()));
        }
        Ok((min, max, avg))
    }
}

impl Drop for LatencyMonitor {
    fn drop(&mut self) {
        unsafe { fg_latency_monitor_free(self.id) };
    }
}

// --- HeartbeatTracker wrapper ---

pub struct HeartbeatTracker {
    id: usize,
}

impl Default for HeartbeatTracker {
    fn default() -> Self {
        Self::new()
    }
}

impl HeartbeatTracker {
    pub fn new() -> Self {
        let id = unsafe { fg_heartbeat_tracker_new() };
        Self { id }
    }

    pub fn received(&self) {
        unsafe { fg_heartbeat_received(self.id) };
    }

    pub fn tick(&self) -> bool {
        unsafe { fg_heartbeat_tick(self.id) == 1 }
    }
}

impl Drop for HeartbeatTracker {
    fn drop(&mut self) {
        unsafe { fg_heartbeat_tracker_free(self.id) };
    }
}

// --- ReconnectState wrapper ---

pub struct ReconnectState {
    id: usize,
}

impl Default for ReconnectState {
    fn default() -> Self {
        Self::new()
    }
}

impl ReconnectState {
    pub fn new() -> Self {
        let id = unsafe { fg_reconnect_state_new() };
        Self { id }
    }

    pub fn start(&self) -> Option<u64> {
        let mut backoff_ms: u64 = 0;
        let mut ok: i32 = 0;
        unsafe { fg_reconnect_start(self.id, &mut backoff_ms, &mut ok) };
        if ok == 1 {
            Some(backoff_ms)
        } else {
            None
        }
    }

    pub fn set_connected(&self) {
        unsafe { fg_reconnect_set_connected(self.id) };
    }

    pub fn reset(&self) {
        unsafe { fg_reconnect_reset(self.id) };
    }
}

impl Drop for ReconnectState {
    fn drop(&mut self) {
        unsafe { fg_reconnect_state_free(self.id) };
    }
}

// --- SequenceTracker wrapper ---

pub struct SequenceTracker {
    id: usize,
}

impl SequenceTracker {
    pub fn new(seq_init: u32) -> Self {
        let id = unsafe { fg_sequence_tracker_new(seq_init) };
        Self { id }
    }

    pub fn next(&self) -> u16 {
        unsafe { fg_sequence_next(self.id) }
    }

    pub fn check(&self, received: u16) -> Result<()> {
        let rc = unsafe { fg_sequence_check(self.id, received) };
        if rc != 0 {
            return Err(FlowGridError::SequenceGap);
        }
        Ok(())
    }

    pub fn reset(&self, seq_init: u32) {
        unsafe { fg_sequence_reset(self.id, seq_init) };
    }
}

impl Drop for SequenceTracker {
    fn drop(&mut self) {
        unsafe { fg_sequence_tracker_free(self.id) };
    }
}

// --- FragmentReassembler wrapper ---

pub struct FragmentReassembler {
    id: usize,
}

impl Default for FragmentReassembler {
    fn default() -> Self {
        Self::new()
    }
}

impl FragmentReassembler {
    pub fn new() -> Self {
        let id = unsafe { fg_fragment_reassembler_new() };
        Self { id }
    }

    pub fn feed(&self, data: &[u8]) -> Result<Option<Vec<u8>>> {
        let mut out_len: usize = 0;
        let mut out_data: *mut u8 = std::ptr::null_mut();
        let rc = unsafe { fg_fragment_feed(self.id, data.as_ptr(), data.len(), &mut out_len, &mut out_data) };
        if rc < 0 {
            return Err(FlowGridError::ProtocolFfi("fragment_feed failed".into()));
        }
        if rc == 0 {
            return Ok(None); // more fragments needed
        }
        let result = unsafe { std::slice::from_raw_parts(out_data, out_len).to_vec() };
        unsafe { fg_free_buffer(out_data as *mut c_void) };
        Ok(Some(result))
    }

    pub fn cleanup(&self) {
        unsafe { fg_fragment_cleanup(self.id) };
    }
}

impl Drop for FragmentReassembler {
    fn drop(&mut self) {
        unsafe { fg_fragment_reassembler_free(self.id) };
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_encode_decode_keydown() {
        let payload = payload_keydown(0x0041, 0x02).unwrap(); // 'A' with shift
        let frame = encode_frame(0x01, 1, 1234567890, &payload).unwrap();
        assert!(!frame.is_empty());

        let (ft, seq, ts, decoded_payload) = decode_frame(&frame).unwrap();
        assert_eq!(ft, 0x01);
        assert_eq!(seq, 1);
        assert_eq!(ts, 1234567890);
        let (key_code, modifiers) = unmarshal_keydown(&decoded_payload).unwrap();
        assert_eq!(key_code, 0x0041);
        assert_eq!(modifiers, 0x02);
    }

    #[test]
    fn test_encode_decode_keyup() {
        let payload = payload_keyup(0x0041).unwrap();
        let frame = encode_frame(0x02, 2, 1234567890, &payload).unwrap();
        let (ft, _, _, decoded_payload) = decode_frame(&frame).unwrap();
        assert_eq!(ft, 0x02);
        let key_code = unmarshal_keyup(&decoded_payload).unwrap();
        assert_eq!(key_code, 0x0041);
    }

    #[test]
    fn test_encode_decode_mousemove() {
        let payload = payload_mousemove(100, -50).unwrap();
        let frame = encode_frame(0x03, 3, 1234567890, &payload).unwrap();
        let (ft, _, _, decoded_payload) = decode_frame(&frame).unwrap();
        assert_eq!(ft, 0x03);
        let (dx, dy) = unmarshal_mousemove(&decoded_payload).unwrap();
        assert_eq!(dx, 100);
        assert_eq!(dy, -50);
    }

    #[test]
    fn test_encode_decode_mousebtn() {
        let payload = payload_mousebtn(1, 1).unwrap();
        let frame = encode_frame(0x04, 4, 1234567890, &payload).unwrap();
        let (ft, _, _, decoded_payload) = decode_frame(&frame).unwrap();
        assert_eq!(ft, 0x04);
        let (btn, state) = unmarshal_mousebtn(&decoded_payload).unwrap();
        assert_eq!(btn, 1);
        assert_eq!(state, 1);
    }

    #[test]
    fn test_encode_decode_scroll() {
        let payload = payload_scroll(3, -1).unwrap();
        let frame = encode_frame(0x05, 5, 1234567890, &payload).unwrap();
        let (ft, _, _, decoded_payload) = decode_frame(&frame).unwrap();
        assert_eq!(ft, 0x05);
        let (dy, dx) = unmarshal_scroll(&decoded_payload).unwrap();
        assert_eq!(dy, 3);
        assert_eq!(dx, -1);
    }

    #[test]
    fn test_encode_decode_latency_ping() {
        let payload = payload_latency_ping(999999).unwrap();
        let frame = encode_frame(0x0A, 10, 1234567890, &payload).unwrap();
        let (ft, _, _, decoded_payload) = decode_frame(&frame).unwrap();
        assert_eq!(ft, 0x0A);
        assert!(!decoded_payload.is_empty());
    }

    #[test]
    fn test_encode_decode_disconnect() {
        let payload = payload_disconnect(0x01).unwrap();
        let frame = encode_frame(0x0D, 13, 1234567890, &payload).unwrap();
        let (ft, _, _, decoded_payload) = decode_frame(&frame).unwrap();
        assert_eq!(ft, 0x0D);
        let reason = unmarshal_disconnect(&decoded_payload).unwrap();
        assert_eq!(reason, 0x01);
    }

    #[test]
    fn test_ble_advertising_roundtrip() {
        let encoded = ble_advertising_encode(1, 3, 0x0F, 36).unwrap();
        let (ver, platform, caps, chan) = ble_advertising_decode(&encoded).unwrap();
        assert_eq!(ver, 1);
        assert_eq!(platform, 3);
        assert_eq!(caps, 0x0F);
        assert_eq!(chan, 36);
    }

    #[test]
    fn test_wifidirect_request_encode() {
        let result = wifidirect_request_encode(
            1, 0x1234,
            [192, 168, 1, 100],
            [255, 255, 255, 0],
            36, 1, 3,
            [0xAA, 0xBB, 0xCC, 0xDD],
        );
        assert!(result.is_ok());
    }

    #[test]
    fn test_wifidirect_response_encode() {
        let result = wifidirect_response_encode(1, 0, 0x1234, 36, 1);
        assert!(result.is_ok());
    }

    #[test]
    fn test_classify_rssi() {
        assert!(classify_rssi(-30) > 0); // excellent
        assert!(classify_rssi(-80) > 0); // poor but valid
    }

    #[test]
    fn test_error_string() {
        let s = error_string(0x01);
        assert!(!s.is_empty());
    }

    #[test]
    fn test_remap_modifiers() {
        let remapped = remap_modifiers(0x02, 1, 3); // shift from macOS to Linux
        assert_eq!(remapped, 0x02); // shift should remain shift
    }

    #[test]
    fn test_latency_monitor() {
        let monitor = LatencyMonitor::new(10);
        monitor.record_ping(1000);
        let (ms, jitter) = monitor.record_pong(1000, 2000).unwrap();
        assert!(ms >= 0.0);
        assert!(jitter >= 0.0);
        let (latest_ms, latest_jitter, quality) = monitor.latest().unwrap();
        assert!(latest_ms >= 0.0);
        assert!(latest_jitter >= 0.0);
        assert!(quality >= 0);
        let (min, max, avg) = monitor.stats().unwrap();
        assert!(min >= 0.0);
        assert!(max >= min);
        assert!(avg >= min && avg <= max);
    }

    #[test]
    fn test_heartbeat_tracker() {
        let tracker = HeartbeatTracker::new();
        tracker.received();
        assert!(!tracker.tick());
    }

    #[test]
    fn test_reconnect_state() {
        let state = ReconnectState::new();
        let backoff = state.start();
        assert!(backoff.is_some());
        state.set_connected();
        state.reset();
    }

    #[test]
    fn test_sequence_tracker() {
        let tracker = SequenceTracker::new(100);
        let seq1 = tracker.next();
        let seq2 = tracker.next();
        assert_eq!(seq2, seq1.wrapping_add(1));
        assert!(tracker.check(seq1).is_ok());
        assert!(tracker.check(seq2).is_ok());
    }

    #[test]
    fn test_fragment_reassembler() {
        let reassembler = FragmentReassembler::new();
        // Small frame may or may not be returned immediately depending on Go fragment header logic.
        // Just verify feed doesn't error and cleanup works.
        let result = reassembler.feed(b"small frame").unwrap();
        // The reassembler might return None if the frame doesn't have fragment headers
        // indicating it's the final fragment, or Some if it passes through directly.
        let _ = result;
        reassembler.cleanup();
    }

    #[test]
    fn test_identify_payload() {
        let result = identify_payload(1, 3, 1, 0x0F, 100, "TestDevice");
        assert!(result.is_ok());
    }

    #[test]
    fn test_identify_response() {
        let result = identify_response(0, 3, 0x0F, "TestDevice");
        assert!(result.is_ok());
    }
}
