package protocol

import (
	"bytes"
	"encoding/hex"
	"testing"
)

// TestHKDFRFC5869Vector verifies hkdfExtract/hkdfExpand against RFC 5869
// Appendix A.1 (HKDF-SHA256, explicit salt/info).
func TestHKDFRFC5869Vector(t *testing.T) {
	ikm := bytes.Repeat([]byte{0x0b}, 22)
	salt := []byte{0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c}
	info := []byte{0xf0, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9}

	wantPRK, _ := hex.DecodeString("077709362c2e32df0ddc3f0dc47bba6390b6c73bb50f9c3122ec844ad7c2b3e5")
	wantOKM, _ := hex.DecodeString("3cb25f25faacd57a90434f64d0362f2a2d2d0a90cf1a5a4c5db02d56ecc4c5bf34007208d5b887185865")

	prk := hkdfExtract(salt, ikm)
	if !bytes.Equal(prk, wantPRK) {
		t.Errorf("PRK: got %x, want %x", prk, wantPRK)
	}

	okm := hkdfExpand(prk, info, 42)
	if !bytes.Equal(okm, wantOKM) {
		t.Errorf("OKM: got %x, want %x", okm, wantOKM)
	}
}

func TestECDHSharedSecretSymmetry(t *testing.T) {
	alice, err := GenerateKeyPair()
	if err != nil {
		t.Fatalf("GenerateKeyPair: %v", err)
	}
	bob, err := GenerateKeyPair()
	if err != nil {
		t.Fatalf("GenerateKeyPair: %v", err)
	}

	secretAB, err := ECDHSharedSecret(alice, bob.PublicKey())
	if err != nil {
		t.Fatalf("ECDHSharedSecret: %v", err)
	}
	secretBA, err := ECDHSharedSecret(bob, alice.PublicKey())
	if err != nil {
		t.Fatalf("ECDHSharedSecret: %v", err)
	}

	if len(secretAB) != SharedSecretSize {
		t.Errorf("shared secret size: got %d, want %d", len(secretAB), SharedSecretSize)
	}
	if !bytes.Equal(secretAB, secretBA) {
		t.Error("both peers must derive the same shared secret")
	}
}

func TestDeriveSessionKeyDeterministic(t *testing.T) {
	secret := bytes.Repeat([]byte{0x42}, SharedSecretSize)
	k1 := DeriveSessionKey(secret)
	k2 := DeriveSessionKey(secret)

	if len(k1) != SessionKeySize {
		t.Errorf("session key size: got %d, want %d", len(k1), SessionKeySize)
	}
	if !bytes.Equal(k1, k2) {
		t.Error("session key derivation must be deterministic")
	}
	other := DeriveSessionKey(bytes.Repeat([]byte{0x43}, SharedSecretSize))
	if bytes.Equal(k1, other) {
		t.Error("different secrets must derive different keys")
	}
}

func TestPublicKeyExportParseRoundTrip(t *testing.T) {
	priv, err := GenerateKeyPair()
	if err != nil {
		t.Fatalf("GenerateKeyPair: %v", err)
	}

	exported := ExportPublicKey(priv.PublicKey())
	if len(exported) != PublicKeySize {
		t.Fatalf("exported key size: got %d, want %d", len(exported), PublicKeySize)
	}
	if exported[0] != 0x04 {
		t.Errorf("uncompressed point must start with 0x04, got 0x%02X", exported[0])
	}

	parsed, err := ParsePublicKey(exported)
	if err != nil {
		t.Fatalf("ParsePublicKey: %v", err)
	}
	if !bytes.Equal(ExportPublicKey(parsed), exported) {
		t.Error("round-trip mismatch")
	}

	if _, err := ParsePublicKey(exported[:32]); err == nil {
		t.Error("expected error for short public key")
	}
}

func TestFingerprint(t *testing.T) {
	priv, err := GenerateKeyPair()
	if err != nil {
		t.Fatalf("GenerateKeyPair: %v", err)
	}

	fp := PublicKeyFingerprint(priv.PublicKey())
	if len(fp) != FingerprintSize*2 {
		t.Fatalf("fingerprint length: got %d, want %d hex chars", len(fp), FingerprintSize*2)
	}
	if !VerifyFingerprint(priv.PublicKey(), fp) {
		t.Error("fingerprint must verify against its own key")
	}
	if VerifyFingerprint(priv.PublicKey(), "00000000000000000000000000000000") {
		t.Error("wrong fingerprint must not verify")
	}
	if VerifyFingerprint(priv.PublicKey(), "not-hex") {
		t.Error("malformed fingerprint must not verify")
	}

	other, _ := GenerateKeyPair()
	if VerifyFingerprint(other.PublicKey(), fp) {
		t.Error("fingerprint must not verify against a different key")
	}
}
