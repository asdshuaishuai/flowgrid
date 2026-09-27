//! Length-prefix framing for DirectLink TCP mode.
//! Format: [Length: u16 BE] [HID Frame]
use crate::error::{FlowGridError, Result};

pub fn encode_length_prefix(data: &[u8]) -> Vec<u8> {
    let len = data.len() as u16;
    let mut buf = Vec::with_capacity(2 + data.len());
    buf.extend_from_slice(&len.to_be_bytes());
    buf.extend_from_slice(data);
    buf
}

pub fn decode_length_prefix(data: &[u8]) -> Result<(Vec<u8>, usize)> {
    if data.len() < 2 {
        return Err(FlowGridError::ProtocolFfi("Length prefix too short".into()));
    }
    let len = u16::from_be_bytes([data[0], data[1]]) as usize;
    let total = 2 + len;
    if data.len() < total {
        return Err(FlowGridError::ProtocolFfi("Incomplete frame".into()));
    }
    let frame = data[2..total].to_vec();
    Ok((frame, total))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_length_prefix_roundtrip() {
        let payload = vec![0x01, 0x00, 0x42, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x08];
        let encoded = encode_length_prefix(&payload);
        assert_eq!(encoded[0..2], [0x00, 0x0E]); // 14 bytes
        let (decoded, consumed) = decode_length_prefix(&encoded).unwrap();
        assert_eq!(decoded, payload);
        assert_eq!(consumed, encoded.len());
    }
}