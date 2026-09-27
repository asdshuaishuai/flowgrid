//! DTLS certificate management and TOFU (Trust-On-First-Use) fingerprint store.

use crate::error::{FlowGridError, Result};
use openssl::ec::{EcGroup, EcKey};
use openssl::nid::Nid;
use openssl::pkey::PKey;
use openssl::x509::{X509Builder, X509NameBuilder, X509};
use std::collections::HashMap;
use std::net::SocketAddr;
use std::path::PathBuf;
use tracing::{info, warn};

const CERT_FILE: &str = "cert.pem";
const KEY_FILE: &str = "key.pem";
const TRUSTED_PEERS_FILE: &str = "trusted_peers.json";

use std::sync::OnceLock;

static DEFAULT_CONFIG_DIR: OnceLock<PathBuf> = OnceLock::new();

/// Set the global default config directory for certificate management.
/// Should be called once during application startup.
pub fn set_default_config_dir(dir: PathBuf) {
    let _ = DEFAULT_CONFIG_DIR.set(dir);
}

pub struct CertManager {
    cert_path: PathBuf,
    key_path: PathBuf,
    trusted_peers_path: PathBuf,
}

impl CertManager {
    pub fn new(config_dir: &std::path::Path) -> Self {
        Self {
            cert_path: config_dir.join(CERT_FILE),
            key_path: config_dir.join(KEY_FILE),
            trusted_peers_path: config_dir.join(TRUSTED_PEERS_FILE),
        }
    }

    /// Create a CertManager using the global default config directory.
    pub fn new_default() -> Self {
        let dir = DEFAULT_CONFIG_DIR
            .get()
            .cloned()
            .unwrap_or_else(|| std::env::temp_dir().join("flowgrid-default-certs"));
        Self::new(&dir)
    }

    /// Generate a self-signed ECDSA P-256 certificate if one does not exist.
    pub fn ensure_cert(&self) -> Result<()> {
        if self.cert_path.exists() && self.key_path.exists() {
            info!("DTLS certificate already exists at {}", self.cert_path.display());
            return Ok(());
        }

        info!("Generating self-signed ECDSA P-256 DTLS certificate...");

        let group = EcGroup::from_curve_name(Nid::X9_62_PRIME256V1)
            .map_err(|e| FlowGridError::transport(format!("EC group failed: {e}")))?;
        let ec_key = EcKey::generate(&group)
            .map_err(|e| FlowGridError::transport(format!("EC key generation failed: {e}")))?;
        let pkey = PKey::from_ec_key(ec_key)
            .map_err(|e| FlowGridError::transport(format!("PKey from EC failed: {e}")))?;

        let mut name_builder = X509NameBuilder::new()
            .map_err(|e| FlowGridError::transport(format!("X509NameBuilder failed: {e}")))?;
        name_builder.append_entry_by_text("CN", "FlowGrid")
            .map_err(|e| FlowGridError::transport(format!("X509Name entry failed: {e}")))?;
        let name = name_builder.build();

        let mut builder = X509Builder::new()
            .map_err(|e| FlowGridError::transport(format!("X509Builder failed: {e}")))?;
        builder.set_version(2)
            .map_err(|e| FlowGridError::transport(format!("set_version failed: {e}")))?;
        builder.set_subject_name(&name)
            .map_err(|e| FlowGridError::transport(format!("set_subject_name failed: {e}")))?;
        builder.set_issuer_name(&name)
            .map_err(|e| FlowGridError::transport(format!("set_issuer_name failed: {e}")))?;
        builder.set_pubkey(&pkey)
            .map_err(|e| FlowGridError::transport(format!("set_pubkey failed: {e}")))?;

        let not_before = openssl::asn1::Asn1Time::days_from_now(0)
            .map_err(|e| FlowGridError::transport(format!("not_before failed: {e}")))?;
        let not_after = openssl::asn1::Asn1Time::days_from_now(365)
            .map_err(|e| FlowGridError::transport(format!("not_after failed: {e}")))?;
        builder.set_not_before(&not_before)
            .map_err(|e| FlowGridError::transport(format!("set_not_before failed: {e}")))?;
        builder.set_not_after(&not_after)
            .map_err(|e| FlowGridError::transport(format!("set_not_after failed: {e}")))?;

        builder.sign(&pkey, openssl::hash::MessageDigest::sha256())
            .map_err(|e| FlowGridError::transport(format!("Certificate signing failed: {e}")))?;

        let cert = builder.build();

        let cert_pem = cert.to_pem()
            .map_err(|e| FlowGridError::transport(format!("cert to_pem failed: {e}")))?;
        std::fs::write(&self.cert_path, cert_pem)
            .map_err(|e| FlowGridError::io(format!("Write cert: {e}")))?;

        let key_pem = pkey.private_key_to_pem_pkcs8()
            .map_err(|e| FlowGridError::transport(format!("key to_pem failed: {e}")))?;
        std::fs::write(&self.key_path, key_pem)
            .map_err(|e| FlowGridError::io(format!("Write key: {e}")))?;

        info!("DTLS certificate saved to {}", self.cert_path.display());
        Ok(())
    }

    /// Load the certificate and private key from disk.
    pub fn load_cert(&self) -> Result<(X509, PKey<openssl::pkey::Private>)> {
        let cert_pem = std::fs::read(&self.cert_path)
            .map_err(|e| FlowGridError::io(format!("Read cert: {e}")))?;
        let key_pem = std::fs::read(&self.key_path)
            .map_err(|e| FlowGridError::io(format!("Read key: {e}")))?;

        let cert = X509::from_pem(&cert_pem)
            .map_err(|e| FlowGridError::transport(format!("Parse cert: {e}")))?;
        let key = PKey::private_key_from_pem(&key_pem)
            .map_err(|e| FlowGridError::transport(format!("Parse key: {e}")))?;

        Ok((cert, key))
    }

    /// Compute the SHA-256 fingerprint of a certificate.
    pub fn fingerprint(cert: &X509) -> Vec<u8> {
        let digest = cert.digest(openssl::hash::MessageDigest::sha256())
            .expect("SHA-256 digest of cert should always succeed");
        digest.as_ref().to_vec()
    }

    /// Check if a peer fingerprint is already trusted.
    /// Returns Ok(true) if trusted and matches, Ok(false) if not yet seen,
    /// Err if stored fingerprint does not match (possible MITM).
    pub fn verify_peer(&self, addr: &SocketAddr, fingerprint: &[u8]) -> Result<bool> {
        let mut store = self.load_trusted_peers()?;
        let addr_str = addr.to_string();
        match store.get(&addr_str) {
            Some(stored) => {
                let stored_bytes = hex::decode(stored)
                    .map_err(|e| FlowGridError::transport(format!("Invalid stored fingerprint: {e}")))?;
                if stored_bytes == fingerprint {
                    Ok(true)
                } else {
                    Err(FlowGridError::transport(
                        "Peer fingerprint mismatch — possible MITM attack".to_string()
                    ))
                }
            }
            None => {
                warn!("New peer {addr} not in trust store; accepting (TOFU)");
                let hex_fp = hex::encode(fingerprint);
                store.insert(addr_str, hex_fp);
                self.save_trusted_peers(&store)?;
                Ok(false)
            }
        }
    }

    fn load_trusted_peers(&self) -> Result<HashMap<String, String>> {
        if !self.trusted_peers_path.exists() {
            return Ok(HashMap::new());
        }
        let data = std::fs::read_to_string(&self.trusted_peers_path)
            .map_err(|e| FlowGridError::io(format!("Read trusted peers: {e}")))?;
        let store: HashMap<String, String> = serde_json::from_str(&data)
            .map_err(|e| FlowGridError::config(format!("Parse trusted peers: {e}")))?;
        Ok(store)
    }

    fn save_trusted_peers(&self, store: &HashMap<String, String>) -> Result<()> {
        let data = serde_json::to_string_pretty(store)
            .map_err(|e| FlowGridError::config(format!("Serialize trusted peers: {e}")))?;
        std::fs::write(&self.trusted_peers_path, data)
            .map_err(|e| FlowGridError::io(format!("Write trusted peers: {e}")))?;
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_generate_and_load_cert() {
        let tmp = std::env::temp_dir().join("flowgrid-cert-test");
        let _ = std::fs::remove_dir_all(&tmp);
        let _ = std::fs::create_dir_all(&tmp);
        let mgr = CertManager::new(&tmp);
        mgr.ensure_cert().unwrap();
        assert!(mgr.cert_path.exists());
        assert!(mgr.key_path.exists());

        let (cert, key) = mgr.load_cert().unwrap();
        assert!(cert.public_key().unwrap().rsa().is_err()); // it's EC, not RSA
        assert!(key.ec_key().is_ok());

        let fp = CertManager::fingerprint(&cert);
        assert_eq!(fp.len(), 32); // SHA-256

        // Verify TOFU flow
        let addr: SocketAddr = "127.0.0.1:12345".parse().unwrap();
        let is_trusted = mgr.verify_peer(&addr, &fp).unwrap();
        assert!(!is_trusted); // first time: accepted but not trusted

        let is_trusted = mgr.verify_peer(&addr, &fp).unwrap();
        assert!(is_trusted); // second time: trusted

        // Wrong fingerprint should fail
        let mut wrong_fp = fp.clone();
        wrong_fp[0] ^= 0xFF;
        assert!(mgr.verify_peer(&addr, &wrong_fp).is_err());

        let _ = std::fs::remove_dir_all(&tmp);
    }
}
