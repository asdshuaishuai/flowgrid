## ADDED Requirements

### Requirement: Per-frame HMAC-SHA256 integrity verification
The system SHALL append a 4-byte HMAC-SHA256 truncated value to every HID frame transmitted over NearLink (WiFi Direct / UDP).

#### Scenario: Frame encoding with HMAC
- **WHEN** a HID frame is encoded for transmission over NearLink
- **THEN** the system computes `HMAC = SHA256(session_key || frame_type || seq || timestamp || payload)[0:4]`, appends it after the payload, and the total frame length becomes `12 + N + 4` bytes

#### Scenario: Frame decoding verifies HMAC
- **WHEN** a HID frame is received over NearLink
- **THEN** the system extracts the last 4 bytes as HMAC, recomputes the expected HMAC using the session key, and if mismatched, SHALL discard the frame and emit an `ERROR (0xFF, code=0x10)` frame to the sender

#### Scenario: HMAC on non-NearLink transports
- **WHEN** a frame is transmitted over DirectLink TCP with TLS 1.3 or Raw Ethernet with physical isolation
- **THEN** the HMAC MAY be omitted since TLS 1.3 provides its own integrity, and physical isolation provides channel security

### Requirement: Session key derivation for HMAC
The system SHALL derive a session-specific HMAC key from the DTLS handshake.

#### Scenario: Key derivation after handshake
- **WHEN** the DTLS handshake completes successfully
- **THEN** the system derives `hmac_key = HKDF-SHA256(dtls_session_key, salt="FlowGrid-v1-HMAC", info="frame-integrity")` and uses this key for all subsequent frame HMAC computation

#### Scenario: Key rotation on reconnect
- **WHEN** a reconnection occurs with a new DTLS handshake
- **THEN** the system SHALL replace the old HMAC key with the newly derived key
