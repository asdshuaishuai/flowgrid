package protocol

import (
	"crypto/ecdh"
	"crypto/hmac"
	"crypto/rand"
	"crypto/sha256"
	"crypto/subtle"
	"encoding/hex"
	"fmt"
	"hash"
)

// First-pairing key exchange (§4.3).
//
// Wire formats (fixed here so every platform binds identically):
//   - Public key:  SEC1 uncompressed point, 65 bytes (0x04 || X || Y)
//   - Shared secret: ECDH output, 32 bytes (P-256 x-coordinate)
//   - Session key:   HKDF-SHA256(sharedSecret, salt="FlowGrid-v1", info="session-key"), 32 bytes
//   - Fingerprint:   hex(SHA-256(publicKeyBytes)[0:16]) — 32 hex chars, persisted to trust peers

const (
	PublicKeySize    = 65
	SharedSecretSize = 32
	SessionKeySize   = 32
	FingerprintSize  = 16
)

var (
	hkdfSalt = []byte("FlowGrid-v1")
	hkdfInfo = []byte("session-key")
)

// GenerateKeyPair creates a fresh ephemeral ECDH P-256 key pair.
func GenerateKeyPair() (*ecdh.PrivateKey, error) {
	return ecdh.P256().GenerateKey(rand.Reader)
}

// ExportPublicKey encodes a public key as the 65-byte SEC1 uncompressed point.
func ExportPublicKey(pub *ecdh.PublicKey) []byte {
	return pub.Bytes()
}

// ParsePublicKey decodes a 65-byte SEC1 uncompressed point.
func ParsePublicKey(data []byte) (*ecdh.PublicKey, error) {
	if len(data) != PublicKeySize {
		return nil, fmt.Errorf("public key must be %d bytes, got %d", PublicKeySize, len(data))
	}
	return ecdh.P256().NewPublicKey(data)
}

// ECDHSharedSecret computes the shared secret with our private key and the peer's
// public key. Both peers derive the same 32 bytes.
func ECDHSharedSecret(priv *ecdh.PrivateKey, peerPub *ecdh.PublicKey) ([]byte, error) {
	secret, err := priv.ECDH(peerPub)
	if err != nil {
		return nil, fmt.Errorf("ECDH: %w", err)
	}
	return secret, nil
}

func hmacSHA256(key []byte) hash.Hash {
	return hmac.New(sha256.New, key)
}

// hkdfExtract implements RFC 5869 HKDF-Extract with SHA-256.
func hkdfExtract(salt, ikm []byte) []byte {
	if len(salt) == 0 {
		salt = make([]byte, sha256.Size)
	}
	mac := hmacSHA256(salt)
	mac.Write(ikm)
	return mac.Sum(nil)
}

// hkdfExpand implements RFC 5869 HKDF-Expand with SHA-256.
func hkdfExpand(prk, info []byte, length int) []byte {
	out := make([]byte, 0, length)
	var prev []byte
	for i := 1; len(out) < length; i++ {
		mac := hmacSHA256(prk)
		mac.Write(prev)
		mac.Write(info)
		mac.Write([]byte{byte(i)})
		prev = mac.Sum(nil)
		out = append(out, prev...)
	}
	return out[:length]
}

// DeriveSessionKey derives the 32-byte session key from the ECDH shared secret
// (§4.3 step 4). The session key is the HMAC key for frame integrity (§8.3).
func DeriveSessionKey(sharedSecret []byte) []byte {
	prk := hkdfExtract(hkdfSalt, sharedSecret)
	return hkdfExpand(prk, hkdfInfo, SessionKeySize)
}

// PublicKeyFingerprint returns the hex-encoded SHA-256 fingerprint prefix of a
// public key (§4.3 step 5). Persist it to recognize and auto-trust paired peers.
func PublicKeyFingerprint(pub *ecdh.PublicKey) string {
	sum := sha256.Sum256(pub.Bytes())
	return hex.EncodeToString(sum[:FingerprintSize])
}

// VerifyFingerprint checks a peer's public key against a persisted fingerprint
// in constant time.
func VerifyFingerprint(pub *ecdh.PublicKey, fingerprint string) bool {
	expected, err := hex.DecodeString(fingerprint)
	if err != nil || len(expected) != FingerprintSize {
		return false
	}
	sum := sha256.Sum256(pub.Bytes())
	return subtle.ConstantTimeCompare(sum[:FingerprintSize], expected) == 1
}
