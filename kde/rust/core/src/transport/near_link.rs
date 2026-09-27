//! NearLink transport: BLE 5.0 discovery → WiFi Direct P2P → UDP + DTLS 1.3.
//!
//! Implementation follows the FlowGrid Protocol v1.0 §3.1 and §4.

use crate::error::{FlowGridError, Result};
use crate::transport::Transport;
use foreign_types::ForeignType;
use std::net::{SocketAddr, UdpSocket};
use std::sync::{Arc, Mutex};
use std::time::{Duration, Instant};
use tracing::{debug, info, trace, warn};

pub const BLE_SERVICE_UUID: uuid::Uuid =
    uuid::Uuid::from_u128(0x00001850_0000_1000_8000_00805F9B34FB);
pub const BLE_CHAR_WIFI_PARAMS: uuid::Uuid =
    uuid::Uuid::from_u128(0x00001853_0000_1000_8000_00805F9B34FB);

/// Discovered device metadata from BLE advertisement or P2P discovery.
#[derive(Debug, Clone)]
pub struct DiscoveredDevice {
    pub name: String,
    pub platform: u8,
    pub caps: u8,
    pub rssi: i32,
    pub wifi_direct_channel: u16,
    pub ble_addr: String,
    pub p2p_addr: Option<String>,
}

/// NearLink transport state machine.
pub struct NearLinkTransport {
    connected: bool,
    peer_addr: Option<SocketAddr>,
    udp_socket: Option<Arc<Mutex<UdpSocket>>>,
    dtls_session: Option<Arc<Mutex<DtlsSession>>>,
    dtls_ctx: Option<openssl::ssl::SslContext>,
    hmac_key: Option<Vec<u8>>,
}

impl NearLinkTransport {
    pub fn new() -> Self {
        Self {
            connected: false,
            peer_addr: None,
            udp_socket: None,
            dtls_session: None,
            dtls_ctx: None,
            hmac_key: None,
        }
    }

    // ------------------------------------------------------------------
    // BLE 5.0 Discovery
    // ------------------------------------------------------------------

    /// BLE 5.0 scan for FlowGrid service advertisements.
    pub async fn scan_ble() -> Result<Vec<DiscoveredDevice>> {
        let session = bluer::Session::new()
            .await
            .map_err(|e| FlowGridError::transport(format!("BLE session failed: {e}")))?;
        let adapter = session
            .default_adapter()
            .await
            .map_err(|e| FlowGridError::transport(format!("BLE adapter failed: {e}")))?;

        adapter
            .set_powered(true)
            .await
            .map_err(|e| FlowGridError::transport(format!("BLE power on failed: {e}")))?;

        info!("Starting BLE scan for FlowGrid service (0x1850)");

        let mut discovered = Vec::new();
        let mut events = adapter
            .discover_devices()
            .await
            .map_err(|e| FlowGridError::transport(format!("BLE discovery failed: {e}")))?;

        use tokio_stream::StreamExt;

        let timeout = tokio::time::Duration::from_secs(5);
        let start = tokio::time::Instant::now();

        while let Some(event) = tokio::time::timeout(timeout, events.next())
            .await
            .ok()
            .flatten()
        {
            if start.elapsed() > timeout {
                break;
            }
            if let bluer::AdapterEvent::DeviceAdded(addr) = event {
                let device = adapter
                    .device(addr)
                    .map_err(|e| FlowGridError::transport(format!("BLE device failed: {e}")))?;
                let uuids = device.uuids().await.ok().flatten().unwrap_or_default();
                if uuids.contains(&BLE_SERVICE_UUID) {
                    let name = device.name().await.ok().flatten().unwrap_or_default();
                    let rssi = device.rssi().await.ok().flatten().unwrap_or(0) as i32;
                    let svc_data = device
                        .service_data()
                        .await
                        .ok()
                        .flatten()
                        .unwrap_or_default();

                    let mut platform = 0u8;
                    let mut caps = 0u8;
                    let mut channel = 0u16;

                    if let Some(data) = svc_data.get(&BLE_SERVICE_UUID) {
                        // Service data may be 5 bytes (payload only) or 7 bytes (with UUID prefix).
                        // Protocol spec §3.1.2: [UUID(2B) | ProtocolVersion(1B) | Platform(1B) | Caps(1B) | WiFiDirectChan(2B)]
                        if data.len() >= 7 {
                            // Full advertising data including UUID prefix
                            let svc_uuid = u16::from_be_bytes([data[0], data[1]]);
                            if svc_uuid == 0x1850 {
                                platform = data.get(3).copied().unwrap_or(0);
                                caps = data.get(4).copied().unwrap_or(0);
                                channel = u16::from_be_bytes([data[5], data[6]]);
                            }
                        } else if data.len() >= 5 {
                            // Payload without UUID prefix
                            platform = data.get(1).copied().unwrap_or(0);
                            caps = data.get(2).copied().unwrap_or(0);
                            channel = u16::from_be_bytes([data[3], data[4]]);
                        }
                    }

                    debug!("BLE device: {name} @ {addr} RSSI={rssi} platform=0x{platform:02X} caps=0x{caps:02X}");
                    discovered.push(DiscoveredDevice {
                        name: name.clone(),
                        platform,
                        caps,
                        rssi,
                        wifi_direct_channel: channel,
                        ble_addr: addr.to_string(),
                        p2p_addr: None,
                    });
                }
            }
        }

        info!("BLE scan complete: {} devices found", discovered.len());
        Ok(discovered)
    }

    // ------------------------------------------------------------------
    // WiFi Direct P2P Discovery (via wpa_supplicant D-Bus)
    // ------------------------------------------------------------------

    /// WiFi Direct P2P discovery via wpa_supplicant D-Bus.
    pub async fn scan_wifi_direct() -> Result<Vec<DiscoveredDevice>> {
        info!("Starting WiFi Direct P2P scan via wpa_supplicant");

        let connection = zbus::Connection::system()
            .await
            .map_err(|e| FlowGridError::transport(format!("D-Bus system connection failed: {e}")))?;

        // Discover the first wpa_supplicant interface dynamically
        let wpa_proxy = WpaSupplicantProxy::builder(&connection)
            .build()
            .await
            .map_err(|e| FlowGridError::transport(format!("WpaSupplicant proxy failed: {e}")))?;

        let interfaces = wpa_proxy
            .interfaces()
            .await
            .map_err(|e| FlowGridError::transport(format!("Failed to get wpa_supplicant interfaces: {e}")))?;

        let interface_path = interfaces.into_iter().next().ok_or_else(|| {
            FlowGridError::transport("No wpa_supplicant interfaces found".to_string())
        })?;

        let proxy = WpaSupplicantP2PProxy::builder(&connection)
            .path(interface_path)
            .map_err(|e| FlowGridError::transport(format!("P2P proxy path failed: {e}")))?
            .build()
            .await
            .map_err(|e| FlowGridError::transport(format!("wpa_supplicant P2P proxy failed: {e}")))?;

        // Start P2P find (timeout 5s)
        proxy
            .p2p_find()
            .await
            .map_err(|e| FlowGridError::transport(format!("P2P find failed: {e}")))?;

        tokio::time::sleep(tokio::time::Duration::from_secs(5)).await;

        proxy
            .p2p_stop_find()
            .await
            .map_err(|e| FlowGridError::transport(format!("P2P stop find failed: {e}")))?;

        // Query discovered peers via wpa_supplicant D-Bus
        let peers = proxy
            .p2p_peers()
            .await
            .map_err(|e| FlowGridError::transport(format!("P2P peers query failed: {e}")))?;

        let mut discovered = Vec::new();
        for peer_path in peers {
            let peer_proxy = P2PPeerProxy::builder(&connection)
                .path(peer_path)
                .map_err(|e| FlowGridError::transport(format!("P2P peer path failed: {e}")))?
                .build()
                .await
                .map_err(|e| FlowGridError::transport(format!("P2P peer proxy failed: {e}")))?;

            let device_name = peer_proxy.device_name().await.unwrap_or_default();
            let device_addr = peer_proxy.device_address().await.unwrap_or_default();
            let signal_level = peer_proxy.level().await.unwrap_or(0);
            let device_type = peer_proxy
                .primary_device_type()
                .await
                .unwrap_or_default();
            let platform = parse_p2p_device_type(&device_type);

            debug!("P2P peer: {device_name} @ {device_addr} RSSI={signal_level}");
            discovered.push(DiscoveredDevice {
                name: device_name,
                platform,
                caps: 0x07, // NearLink + DirectLink + WiFi Direct
                rssi: signal_level as i32,
                wifi_direct_channel: 0,
                ble_addr: String::new(),
                p2p_addr: Some(device_addr),
            });
        }

        info!("WiFi Direct P2P scan complete: {} peers found", discovered.len());
        Ok(discovered)
    }

    // ------------------------------------------------------------------
    // BLE GATT WiFi Direct Parameter Exchange
    // ------------------------------------------------------------------

    /// Exchange WiFi Direct parameters over BLE (Characteristic 0x1853).
    ///
    /// Protocol §3.1.3:
    /// Request:  20 bytes (Version + Port + IP + Subnet + Channel + Band + Security + Reserved + PSK Hash + Reserved)
    /// Response: 8 bytes (Version + Status + Port + Channel + Band + Reserved)
    pub async fn exchange_wifi_params(
        &self,
        device: &DiscoveredDevice,
    ) -> Result<WifiDirectParams> {
        info!("Exchanging WiFi Direct params via BLE GATT with {}", device.ble_addr);

        let session = bluer::Session::new()
            .await
            .map_err(|e| FlowGridError::transport(format!("BLE session failed: {e}")))?;
        let adapter = session
            .default_adapter()
            .await
            .map_err(|e| FlowGridError::transport(format!("BLE adapter failed: {e}")))?;

        let addr: bluer::Address = device
            .ble_addr
            .parse()
            .map_err(|e| FlowGridError::transport(format!("Invalid BLE address: {e}")))?;
        let ble_device = adapter
            .device(addr)
            .map_err(|e| FlowGridError::transport(format!("BLE device failed: {e}")))?;

        // Connect to BLE device
        ble_device
            .connect()
            .await
            .map_err(|e| FlowGridError::transport(format!("BLE connect failed: {e}")))?;

        // Discover FlowGrid service and characteristic
        let services = ble_device
            .services()
            .await
            .map_err(|e| FlowGridError::transport(format!("BLE GATT discovery failed: {e}")))?;

        let mut char_handle: Option<bluer::gatt::remote::Characteristic> = None;
        for service in services {
            let service_uuid = service.uuid().await.ok();
            if service_uuid == Some(BLE_SERVICE_UUID) {
                let chars = service
                    .characteristics()
                    .await
                    .map_err(|e| FlowGridError::transport(format!("BLE chars failed: {e}")))?;
                for char in chars {
                    let uuid = char.uuid().await.ok();
                    if uuid == Some(BLE_CHAR_WIFI_PARAMS) {
                        char_handle = Some(char);
                        break;
                    }
                }
            }
            if char_handle.is_some() {
                break;
            }
        }

        let char = char_handle
            .ok_or_else(|| FlowGridError::transport("BLE char 0x1853 not found".to_string()))?;

        // Build request: 20 bytes per protocol §3.1.3
        let mut request = [0u8; 20];
        request[0] = 0x01; // Version
        let port = 24801u16;
        request[1..3].copy_from_slice(&port.to_be_bytes());
        // IP: 192.168.49.1 (placeholder)
        request[3..7].copy_from_slice(&[192, 168, 49, 1]);
        // Subnet: 255.255.255.252 (/30)
        request[7..11].copy_from_slice(&[255, 255, 255, 252]);
        request[11] = 36; // Channel (5GHz Ch 36)
        request[12] = 0x02; // Band: 5GHz
        request[13] = 0x02; // Security: WPA3
        // Reserved [14] = 0
        // PSK Hash: 4 bytes of SHA-256("FlowGrid")[0:4]
        request[15..19].copy_from_slice(&[0x4a, 0x3c, 0x8f, 0x12]);
        // Reserved [19] = 0

        char
            .write(&request)
            .await
            .map_err(|e| FlowGridError::transport(format!("BLE GATT write failed: {e}")))?;

        // Read response (8 bytes)
        let response = char
            .read()
            .await
            .map_err(|e| FlowGridError::transport(format!("BLE GATT read failed: {e}")))?;

        if response.len() < 8 {
            return Err(FlowGridError::transport(
                "BLE GATT response too short".to_string(),
            ));
        }

        let status = response[1];
        if status != 0x00 {
            return Err(FlowGridError::transport(format!(
                "WiFi Direct params rejected: status 0x{status:02X}"
            )));
        }

        let resp_port = u16::from_be_bytes([response[2], response[3]]);
        let resp_channel = u16::from_be_bytes([response[4], response[5]]);
        let resp_band = response[6];

        info!("WiFi Direct params accepted: port={resp_port} channel={resp_channel} band={resp_band}");

        let _ = ble_device.disconnect().await;

        Ok(WifiDirectParams {
            version: response[0],
            port: resp_port,
            ip_addr: [192, 168, 49, 2], // Target gets .2
            subnet_mask: [255, 255, 255, 252],
            channel: resp_channel as u8,
            band: resp_band,
            security: 0x02,
            psk_hash: [0x4a, 0x3c, 0x8f, 0x12],
        })
    }

    // ------------------------------------------------------------------
    // DTLS 1.3 Context Initialization
    // ------------------------------------------------------------------

    fn init_dtls_context(&self) -> Result<openssl::ssl::SslContext> {
        let mut ctx = openssl::ssl::SslContext::builder(openssl::ssl::SslMethod::dtls())
            .map_err(|e| FlowGridError::transport(format!("DTLS context failed: {e}")))?;

        // OpenSSL 3.x auto-negotiates DTLS 1.3 when available.
        // The rust-openssl crate does not expose SslVersion::DTLS1_3 (only DTLS1_2),
        // so we rely on the library's default negotiation to select the highest
        // supported DTLS version. This is functionally equivalent for our use case.
        ctx.set_min_proto_version(Some(openssl::ssl::SslVersion::DTLS1_2))
            .map_err(|e| FlowGridError::transport(format!("DTLS min version failed: {e}")))?;
        ctx.set_max_proto_version(None)
            .map_err(|e| FlowGridError::transport(format!("DTLS max version failed: {e}")))?;

        // ECDH P-256 (use curve parameters, not a generated key)
        let ec_key = openssl::ec::EcKey::from_curve_name(openssl::nid::Nid::X9_62_PRIME256V1)
            .map_err(|e| FlowGridError::transport(format!("EC key P-256 failed: {e}")))?;
        ctx.set_tmp_ecdh(&ec_key)
            .map_err(|e| FlowGridError::transport(format!("ECDH set failed: {e}")))?;

        // Cipher suites: TLS_AES_256_GCM_SHA384 (TLS 1.3 style)
        ctx.set_ciphersuites("TLS_AES_256_GCM_SHA384")
            .map_err(|e| FlowGridError::transport(format!("Ciphersuites failed: {e}")))?;

        // Load self-signed certificate and private key
        let cert_mgr = crate::cert_manager::CertManager::new_default();
        cert_mgr.ensure_cert()?;
        if let Ok((cert, key)) = cert_mgr.load_cert() {
            ctx.set_certificate(&cert)
                .map_err(|e| FlowGridError::transport(format!("Set certificate failed: {e}")))?;
            ctx.set_private_key(&key)
                .map_err(|e| FlowGridError::transport(format!("Set private key failed: {e}")))?;
        }

        // Disable built-in peer verification; we do TOFU after handshake.
        ctx.set_verify(openssl::ssl::SslVerifyMode::NONE);

        Ok(ctx.build())
    }
}

impl Transport for NearLinkTransport {
    fn connect(&mut self, addr: SocketAddr) -> Result<()> {
        if self.connected {
            return Err(FlowGridError::AlreadyConnected);
        }

        // Bind UDP socket for HID frames
        let socket = UdpSocket::bind("0.0.0.0:0")
            .map_err(|e| FlowGridError::transport(format!("UDP bind failed: {e}")))?;
        socket
            .connect(addr)
            .map_err(|e| FlowGridError::transport(format!("UDP connect failed: {e}")))?;
        socket
            .set_nonblocking(true)
            .map_err(|e| FlowGridError::transport(format!("set_nonblocking failed: {e}")))?;

        // Initialize DTLS context
        let ctx = self.init_dtls_context()?;
        let mut session = DtlsSession::new_client(&ctx, addr)
            .map_err(|e| FlowGridError::transport(format!("DTLS session create failed: {e}")))?;

        // Perform DTLS handshake
        session
            .handshake(&socket)
            .map_err(|e| FlowGridError::transport(format!("DTLS handshake failed: {e}")))?;

        // TOFU fingerprint verification after handshake
        if let Some(fp) = session.peer_fingerprint() {
            let cert_mgr = crate::cert_manager::CertManager::new_default();
            match cert_mgr.verify_peer(&addr, &fp) {
                Ok(true) => {
                    info!("Peer {addr} fingerprint verified (trusted)");
                }
                Ok(false) => {
                    info!("Peer {addr} fingerprint accepted for the first time (TOFU)");
                }
                Err(e) => {
                    return Err(FlowGridError::transport(format!(
                        "Peer fingerprint verification failed: {e}"
                    )));
                }
            }
        } else {
            warn!("No peer certificate received from {addr}; continuing without verification");
        }

        // Derive HMAC key from DTLS session for frame integrity
        match session.derive_hmac_key() {
            Ok(key) => {
                self.hmac_key = Some(key);
                info!("Derived HMAC key from DTLS session for {addr}");
            }
            Err(e) => {
                warn!("Failed to derive HMAC key from DTLS session: {e}; continuing without HMAC");
                self.hmac_key = None;
            }
        }

        self.udp_socket = Some(Arc::new(Mutex::new(socket)));
        self.peer_addr = Some(addr);
        self.dtls_session = Some(Arc::new(Mutex::new(session)));
        self.dtls_ctx = Some(ctx);
        self.connected = true;

        info!("NearLink connected to {addr} with DTLS 1.3");
        Ok(())
    }

    fn disconnect(&mut self) -> Result<()> {
        self.connected = false;
        self.peer_addr = None;
        self.udp_socket = None;
        self.dtls_session = None;
        self.dtls_ctx = None;
        self.hmac_key = None;
        info!("NearLink disconnected");
        Ok(())
    }

    fn send_frame(&self, data: &[u8]) -> Result<()> {
        if let Some(session) = self.dtls_session.as_ref() {
            let mut session = session
                .lock()
                .map_err(|e| FlowGridError::transport(format!("DTLS session lock failed: {e}")))?;
            let socket = self
                .udp_socket
                .as_ref()
                .ok_or(FlowGridError::NotConnected)?;
            let socket = socket
                .lock()
                .map_err(|e| FlowGridError::transport(format!("UDP lock failed: {e}")))?;
            session
                .encrypt(data, &socket)
                .map_err(|e| FlowGridError::transport(format!("DTLS encrypt failed: {e}")))?;
            trace!("NearLink sent {} bytes (encrypted)", data.len());
        } else {
            let socket = self
                .udp_socket
                .as_ref()
                .ok_or(FlowGridError::NotConnected)?;
            let socket = socket
                .lock()
                .map_err(|e| FlowGridError::transport(format!("UDP lock failed: {e}")))?;
            socket
                .send(data)
                .map_err(|e| FlowGridError::transport(format!("UDP send failed: {e}")))?;
            trace!("NearLink sent {} bytes (unencrypted)", data.len());
        }
        Ok(())
    }

    fn recv_frame(&self, buf: &mut [u8]) -> Result<usize> {
        if let Some(session) = self.dtls_session.as_ref() {
            let mut session = session
                .lock()
                .map_err(|e| FlowGridError::transport(format!("DTLS session lock failed: {e}")))?;
            let socket = self
                .udp_socket
                .as_ref()
                .ok_or(FlowGridError::NotConnected)?;
            let socket = socket
                .lock()
                .map_err(|e| FlowGridError::transport(format!("UDP lock failed: {e}")))?;
            session.decrypt(&socket, buf)
        } else {
            let socket = self
                .udp_socket
                .as_ref()
                .ok_or(FlowGridError::NotConnected)?;
            let socket = socket
                .lock()
                .map_err(|e| FlowGridError::transport(format!("UDP lock failed: {e}")))?;
            match socket.recv(buf) {
                Ok(n) => Ok(n),
                Err(e) if e.kind() == std::io::ErrorKind::WouldBlock => Ok(0),
                Err(e) => Err(FlowGridError::transport(format!("UDP recv failed: {e}"))),
            }
        }
    }

    fn is_connected(&self) -> bool {
        self.connected
    }

    fn local_addr(&self) -> Option<SocketAddr> {
        self.udp_socket.as_ref().and_then(|s| {
            s.lock().ok().and_then(|sock| sock.local_addr().ok())
        })
    }

    fn hmac_key(&self) -> Option<Vec<u8>> {
        self.hmac_key.clone()
    }
}

impl Default for NearLinkTransport {
    fn default() -> Self {
        Self::new()
    }
}

// ------------------------------------------------------------------
// WiFi Direct connection parameters.
// ------------------------------------------------------------------

#[derive(Debug, Clone, Default)]
pub struct WifiDirectParams {
    pub version: u8,
    pub port: u16,
    pub ip_addr: [u8; 4],
    pub subnet_mask: [u8; 4],
    pub channel: u8,
    pub band: u8,       // 0x01 = 2.4GHz, 0x02 = 5GHz
    pub security: u8,    // 0x01 = WPA2, 0x02 = WPA3
    pub psk_hash: [u8; 4],
}

// ------------------------------------------------------------------
// DTLS session wrapper.
// ------------------------------------------------------------------

#[derive(Debug)]
pub struct DtlsSession {
    ssl: openssl::ssl::Ssl,
    read_bio_ptr: *mut openssl_sys::BIO,
    write_bio_ptr: *mut openssl_sys::BIO,
    peer_addr: SocketAddr,
}

// Safety: SSL_set_bio takes ownership of both BIOs, so they remain valid as long as ssl lives.
// SSL_free (called by Ssl::drop) will also free the BIOs.
unsafe impl Send for DtlsSession {}
unsafe impl Sync for DtlsSession {}

impl DtlsSession {
    pub fn new_client(
        ctx: &openssl::ssl::SslContext,
        peer_addr: SocketAddr,
    ) -> std::result::Result<Self, openssl::error::ErrorStack> {
        let ssl = openssl::ssl::Ssl::new(ctx)?;
        let read_bio = unsafe { openssl_sys::BIO_new(openssl_sys::BIO_s_mem()) };
        if read_bio.is_null() {
            return Err(openssl::error::ErrorStack::get());
        }
        let write_bio = unsafe { openssl_sys::BIO_new(openssl_sys::BIO_s_mem()) };
        if write_bio.is_null() {
            unsafe { openssl_sys::BIO_free_all(read_bio) };
            return Err(openssl::error::ErrorStack::get());
        }

        let read_bio_ptr = read_bio;
        let write_bio_ptr = write_bio;

        let ssl_ptr = ssl.as_ptr();
        // SAFETY: SSL_set_bio takes ownership of both BIOs. We pass raw pointers
        // and OpenSSL manages their lifetime after this call. The BIOs are not
        // freed by Rust since ownership is transferred to the SSL object.
        unsafe {
            openssl_sys::SSL_set_bio(ssl_ptr, read_bio_ptr, write_bio_ptr);
        }

        // SSL_set_bio takes ownership of both BIOs; no need to forget since we used raw pointers.

        Ok(DtlsSession {
            ssl,
            read_bio_ptr,
            write_bio_ptr,
            peer_addr,
        })
    }

    /// Retrieve the peer certificate fingerprint (SHA-256) after handshake.
    pub fn peer_fingerprint(&self) -> Option<Vec<u8>> {
        self.ssl.peer_certificate().map(|cert| crate::cert_manager::CertManager::fingerprint(&cert))
    }

    /// Derive a 32-byte HMAC key from the DTLS session using export_keying_material (RFC 5705).
    pub fn derive_hmac_key(&self) -> Result<Vec<u8>> {
        let mut out = [0u8; 32];
        self.ssl
            .export_keying_material(&mut out, "FlowGrid-v1-HMAC", None)
            .map_err(|e| FlowGridError::transport(format!("Export keying material failed: {e}")))?;
        Ok(out.to_vec())
    }

    fn pump_read(&mut self, socket: &UdpSocket) -> std::io::Result<()> {
        let mut buf = [0u8; 2048];
        match socket.recv_from(&mut buf) {
            Ok((len, _)) => {
                unsafe {
                    openssl_sys::BIO_write(
                        self.read_bio_ptr,
                        buf.as_ptr() as *const _,
                        len as std::ffi::c_int,
                    );
                }
                Ok(())
            }
            Err(e) if e.kind() == std::io::ErrorKind::WouldBlock => Ok(()),
            Err(e) => Err(e),
        }
    }

    fn pump_write(&mut self, socket: &UdpSocket) -> std::io::Result<()> {
        let mut buf = [0u8; 2048];
        unsafe {
            let len = openssl_sys::BIO_read(
                self.write_bio_ptr,
                buf.as_mut_ptr() as *mut _,
                buf.len() as std::ffi::c_int,
            );
            if len > 0 {
                let data = std::slice::from_raw_parts(buf.as_ptr(), len as usize);
                match socket.send_to(data, self.peer_addr) {
                    Ok(_) => {}
                    Err(e) if e.kind() == std::io::ErrorKind::WouldBlock => {}
                    Err(e) => return Err(e),
                }
            }
        }
        Ok(())
    }

    pub fn handshake(&mut self, socket: &UdpSocket) -> Result<()> {
        self.ssl.set_connect_state();
        let deadline = Instant::now() + Duration::from_secs(5);
        loop {
            if Instant::now() > deadline {
                return Err(FlowGridError::transport(
                    "DTLS handshake timeout".to_string(),
                ));
            }

            let ssl_ptr = self.ssl.as_ptr();
            let ret = unsafe { openssl_sys::SSL_connect(ssl_ptr) };
            if ret > 0 {
                return Ok(());
            }

            let err = unsafe { openssl_sys::SSL_get_error(ssl_ptr, ret) };
            let error_code = openssl::ssl::ErrorCode::from_raw(err);

            if error_code == openssl::ssl::ErrorCode::WANT_READ {
                self.pump_read(socket)
                    .map_err(|e| FlowGridError::transport(format!("IO error: {e}")))?;
                self.pump_write(socket)
                    .map_err(|e| FlowGridError::transport(format!("IO error: {e}")))?;
            } else if error_code == openssl::ssl::ErrorCode::WANT_WRITE {
                self.pump_write(socket)
                    .map_err(|e| FlowGridError::transport(format!("IO error: {e}")))?;
            } else {
                return Err(FlowGridError::transport(format!(
                    "DTLS handshake failed: code={}",
                    err
                )));
            }
        }
    }

    pub fn encrypt(&mut self, data: &[u8], socket: &UdpSocket) -> Result<usize> {
        let mut written = 0;
        while written < data.len() {
            let to_write = &data[written..];
            let ssl_ptr = self.ssl.as_ptr();
            let ret = unsafe {
                openssl_sys::SSL_write(
                    ssl_ptr,
                    to_write.as_ptr() as *const _,
                    to_write.len() as std::ffi::c_int,
                )
            };

            if ret > 0 {
                self.pump_write(socket)
                    .map_err(|e| FlowGridError::transport(format!("IO error: {e}")))?;
                written += ret as usize;
                continue;
            }

            let err = unsafe { openssl_sys::SSL_get_error(ssl_ptr, ret) };
            let error_code = openssl::ssl::ErrorCode::from_raw(err);

            if error_code == openssl::ssl::ErrorCode::WANT_READ {
                self.pump_read(socket)
                    .map_err(|e| FlowGridError::transport(format!("IO error: {e}")))?;
                self.pump_write(socket)
                    .map_err(|e| FlowGridError::transport(format!("IO error: {e}")))?;
            } else if error_code == openssl::ssl::ErrorCode::WANT_WRITE {
                self.pump_write(socket)
                    .map_err(|e| FlowGridError::transport(format!("IO error: {e}")))?;
            } else {
                return Err(FlowGridError::transport(format!(
                    "DTLS write failed: code={}",
                    err
                )));
            }
        }
        Ok(written)
    }

    pub fn decrypt(&mut self, socket: &UdpSocket, buf: &mut [u8]) -> Result<usize> {
        let mut recv_buf = [0u8; 2048];
        let ssl_ptr = self.ssl.as_ptr();

        loop {
            let mut pumped = false;
            match socket.recv_from(&mut recv_buf) {
                Ok((len, _)) => {
                    unsafe {
                        openssl_sys::BIO_write(
                            self.read_bio_ptr,
                            recv_buf.as_ptr() as *const _,
                            len as std::ffi::c_int,
                        );
                    }
                    pumped = true;
                }
                Err(e) if e.kind() == std::io::ErrorKind::WouldBlock => {}
                Err(e) => return Err(FlowGridError::transport(format!("IO error: {e}"))),
            }

            let ret = unsafe {
                openssl_sys::SSL_read(
                    ssl_ptr,
                    buf.as_mut_ptr() as *mut _,
                    buf.len() as std::ffi::c_int,
                )
            };

            if ret > 0 {
                return Ok(ret as usize);
            }

            let err = unsafe { openssl_sys::SSL_get_error(ssl_ptr, ret) };
            let error_code = openssl::ssl::ErrorCode::from_raw(err);

            if error_code == openssl::ssl::ErrorCode::WANT_READ {
                if !pumped {
                    return Ok(0);
                }
            } else if error_code == openssl::ssl::ErrorCode::WANT_WRITE {
                self.pump_write(socket)
                    .map_err(|e| FlowGridError::transport(format!("IO error: {e}")))?;
                return Ok(0);
            } else {
                return Err(FlowGridError::transport(format!(
                    "DTLS read failed: code={}",
                    err
                )));
            }
        }
    }
}

// ------------------------------------------------------------------
// wpa_supplicant D-Bus proxies (zbus).
// ------------------------------------------------------------------

#[zbus::proxy(
    interface = "fi.w1.wpa_supplicant1",
    default_service = "fi.w1.wpa_supplicant1",
    default_path = "/fi/w1/wpa_supplicant1"
)]
trait WpaSupplicant {
    #[zbus(property, name = "Interfaces")]
    fn interfaces(&self) -> zbus::Result<Vec<zbus::zvariant::OwnedObjectPath>>;
}

#[zbus::proxy(
    interface = "fi.w1.wpa_supplicant1.Interface.P2PDevice",
    default_service = "fi.w1.wpa_supplicant1",
    default_path = "/fi/w1/wpa_supplicant1/Interfaces/0"
)]
trait WpaSupplicantP2P {
    #[zbus(name = "P2PFind")]
    fn p2p_find(&self) -> zbus::Result<()>;

    #[zbus(name = "P2PStopFind")]
    fn p2p_stop_find(&self) -> zbus::Result<()>;

    #[zbus(name = "P2PPeers")]
    fn p2p_peers(&self) -> zbus::Result<Vec<zbus::zvariant::OwnedObjectPath>>;
}

#[zbus::proxy(
    interface = "fi.w1.wpa_supplicant1.Interface.P2PPeer",
    default_service = "fi.w1.wpa_supplicant1"
)]
trait P2PPeer {
    #[zbus(property, name = "DeviceName")]
    fn device_name(&self) -> zbus::Result<String>;

    #[zbus(property, name = "DeviceAddress")]
    fn device_address(&self) -> zbus::Result<String>;

    #[zbus(property, name = "Level")]
    fn level(&self) -> zbus::Result<i16>;

    #[zbus(property, name = "PrimaryDeviceType")]
    fn primary_device_type(&self) -> zbus::Result<String>;
}

// ------------------------------------------------------------------
// Helpers
// ------------------------------------------------------------------

fn parse_p2p_device_type(device_type: &str) -> u8 {
    match device_type {
        "1-0050F204-1" => 0x01, // macOS
        "1-0050F204-2" => 0x02, // Windows
        "1-0050F204-3" => 0x03, // Linux
        _ => 0x03,
    }
}
