//! DirectLink transport: TCP + UDP over LAN.
use crate::error::{FlowGridError, Result};
use crate::transport::Transport;
use std::net::{SocketAddr, TcpStream, UdpSocket};
use std::sync::{Arc, Mutex};
use tracing::{debug, info};

#[allow(dead_code)]
const DEFAULT_PORT: u16 = 24801;
const TCP_READ_TIMEOUT_MS: u64 = 5000;

pub struct DirectLinkTransport {
    udp_socket: Option<Arc<UdpSocket>>,
    tcp_stream: Option<Arc<Mutex<TcpStream>>>,
    peer_addr: Option<SocketAddr>,
    connected: bool,
}

impl DirectLinkTransport {
    pub fn new() -> Self {
        Self {
            udp_socket: None,
            tcp_stream: None,
            peer_addr: None,
            connected: false,
        }
    }

    fn bind_local_udp() -> Result<UdpSocket> {
        let socket = UdpSocket::bind("0.0.0.0:0")
            .map_err(|e| FlowGridError::transport(format!("UDP bind failed: {e}")))?;
        socket
            .set_nonblocking(true)
            .map_err(|e| FlowGridError::transport(format!("UDP set_nonblocking failed: {e}")))?;
        Ok(socket)
    }
}

impl Transport for DirectLinkTransport {
    fn connect(&mut self, addr: SocketAddr) -> Result<()> {
        if self.connected {
            return Err(FlowGridError::AlreadyConnected);
        }

        // UDP socket for HID frames
        let udp = Self::bind_local_udp()?;
        udp.connect(addr)
            .map_err(|e| FlowGridError::transport(format!("UDP connect failed: {e}")))?;
        self.udp_socket = Some(Arc::new(udp));

        // TCP for control channel (DirectLink TCP mode)
        let tcp = TcpStream::connect(addr)
            .map_err(|e| FlowGridError::transport(format!("TCP connect failed: {e}")))?;
        tcp.set_nodelay(true)
            .map_err(|e| FlowGridError::transport(format!("TCP set_nodelay failed: {e}")))?;
        tcp.set_read_timeout(Some(std::time::Duration::from_millis(TCP_READ_TIMEOUT_MS)))
            .map_err(|e| FlowGridError::transport(format!("TCP set_read_timeout failed: {e}")))?;
        self.tcp_stream = Some(Arc::new(Mutex::new(tcp)));
        self.peer_addr = Some(addr);
        self.connected = true;

        info!("DirectLink connected to {addr}");
        Ok(())
    }

    fn disconnect(&mut self) -> Result<()> {
        if !self.connected {
            return Ok(());
        }
        self.connected = false;
        if let Some(tcp) = self.tcp_stream.take()
            && let Ok(stream) = tcp.lock() {
                let _ = stream.shutdown(std::net::Shutdown::Both);
            }
        self.udp_socket = None;
        self.peer_addr = None;
        info!("DirectLink disconnected");
        Ok(())
    }

    fn send_frame(&self, data: &[u8]) -> Result<()> {
        if !self.connected {
            return Err(FlowGridError::NotConnected);
        }

        // Send via UDP (primary HID channel for DirectLink)
        if let Some(udp) = &self.udp_socket {
            udp.send(data)
                .map_err(|e| FlowGridError::transport(format!("UDP send failed: {e}")))?;
            debug!("Sent {} bytes via UDP", data.len());
            return Ok(());
        }

        Err(FlowGridError::transport("No UDP socket available".to_string()))
    }

    fn recv_frame(&self, buf: &mut [u8]) -> Result<usize> {
        if !self.connected {
            return Err(FlowGridError::NotConnected);
        }
        if let Some(udp) = &self.udp_socket {
            match udp.recv(buf) {
                Ok(n) => Ok(n),
                Err(e) if e.kind() == std::io::ErrorKind::WouldBlock => Ok(0),
                Err(e) => Err(FlowGridError::transport(format!("UDP recv failed: {e}"))),
            }
        } else {
            Ok(0)
        }
    }

    fn is_connected(&self) -> bool {
        self.connected
    }

    fn local_addr(&self) -> Option<SocketAddr> {
        self.udp_socket.as_ref().and_then(|s| s.local_addr().ok())
    }
}

impl Default for DirectLinkTransport {
    fn default() -> Self {
        Self::new()
    }
}

impl Drop for DirectLinkTransport {
    fn drop(&mut self) {
        let _ = self.disconnect();
    }
}
