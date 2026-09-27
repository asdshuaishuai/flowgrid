## ADDED Requirements

### Requirement: Self-signed ECDSA P-256 certificate generation
The system SHALL generate a self-signed ECDSA P-256 certificate and private key on first application launch if no certificate exists.

#### Scenario: Fresh install generates certificate
- **WHEN** the application starts and no certificate is found in the config directory
- **THEN** the system generates a new ECDSA P-256 key pair, creates a self-signed X.509 certificate with CN="FlowGrid-{device-id}", and persists both to `~/.config/flowgrid/cert.pem` and `~/.config/flowgrid/key.pem`

#### Scenario: Existing certificate is reused
- **WHEN** the application starts and a valid certificate exists in the config directory
- **THEN** the system loads the existing certificate without generating a new one

### Requirement: Trust-on-first-use fingerprint model
The system SHALL implement a trust-on-first-use (TOFU) model for DTLS peer authentication using certificate fingerprints.

#### Scenario: First connection trusts peer fingerprint
- **WHEN** a device connects for the first time via DTLS
- **THEN** the system computes the SHA-256 fingerprint of the peer's certificate, stores it in `~/.config/flowgrid/trusted_peers.json`, and marks the peer as trusted

#### Scenario: Reconnect verifies fingerprint
- **WHEN** a previously trusted device reconnects via DTLS
- **THEN** the system compares the peer's certificate fingerprint against the stored value; if mismatched, the handshake SHALL fail with error code `0x04` (securityError)

#### Scenario: Fingerprint expiry
- **WHEN** a stored fingerprint is older than 30 days
- **THEN** the system SHALL re-verify the fingerprint on the next connection (not auto-trust)

### Requirement: DTLS 1.3 version enforcement
The system SHALL enforce DTLS 1.3 and reject DTLS 1.2 or lower negotiations.

#### Scenario: DTLS 1.3 handshake succeeds
- **WHEN** both peers support DTLS 1.3 with TLS_AES_256_GCM_SHA384 cipher suite
- **THEN** the handshake completes successfully

#### Scenario: DTLS 1.2 peer is rejected
- **WHEN** a peer attempts to negotiate DTLS 1.2
- **THEN** the handshake SHALL fail with `SSL_R_NO_PROTOCOLS_AVAILABLE`
