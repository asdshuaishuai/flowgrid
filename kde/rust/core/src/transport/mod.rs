pub mod direct_link;
pub mod near_link;
pub mod packet;

use crate::error::Result;
use std::net::SocketAddr;

/// Transport abstraction.
pub trait Transport: Send + Sync {
    fn connect(&mut self, addr: SocketAddr) -> Result<()>;
    fn disconnect(&mut self) -> Result<()>;
    fn send_frame(&self, data: &[u8]) -> Result<()>;
    fn recv_frame(&self, buf: &mut [u8]) -> Result<usize>;
    fn is_connected(&self) -> bool;
    fn local_addr(&self) -> Option<SocketAddr>;
    /// Optional HMAC key for frame integrity (NearLink DTLS-derived).
    fn hmac_key(&self) -> Option<Vec<u8>> {
        None
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum TransportType {
    NearLink,
    DirectLink,
}

impl TransportType {
    pub fn name(&self) -> &'static str {
        match self {
            TransportType::NearLink => "NearLink",
            TransportType::DirectLink => "DirectLink",
        }
    }
}
