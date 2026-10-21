package protocol

import (
	"encoding/hex"
	"testing"
)

func TestLanIDAnnouncementRoundTrip(t *testing.T) {
	ident, err := GenerateIdentityKeyPair()
	if err != nil {
		t.Fatalf("GenerateIdentityKeyPair: %v", err)
	}
	ann := &LanIDAnnouncement{
		Version:  1,
		Platform: PlatformMacOS,
		Caps:     CapDirectLink | CapClipboard,
		Port:     24801,
		Name:     "Test Mac",
		PubKey:   IdentityPublicKeyBytes(ident),
	}
	wire, err := ann.Sign(ident)
	if err != nil {
		t.Fatalf("Sign: %v", err)
	}
	got, err := UnmarshalLanIDAnnouncement(wire)
	if err != nil {
		t.Fatalf("UnmarshalLanIDAnnouncement: %v", err)
	}
	if got.Version != 1 || got.Platform != PlatformMacOS || got.Port != 24801 || got.Name != "Test Mac" {
		t.Fatalf("round-trip mismatch: %+v", got)
	}
	if hex.EncodeToString(got.PubKey) != hex.EncodeToString(ann.PubKey) {
		t.Fatal("pubkey mismatch")
	}
}

func TestLanIDAnnouncementTampered(t *testing.T) {
	ident, _ := GenerateIdentityKeyPair()
	ann := &LanIDAnnouncement{
		Version: 1, Platform: PlatformMacOS, Caps: CapDirectLink,
		Port: 24801, Name: "A", PubKey: IdentityPublicKeyBytes(ident),
	}
	wire, _ := ann.Sign(ident)

	// 篡改签名字节
	bad := append([]byte(nil), wire...)
	bad[len(bad)-1] ^= 0xFF
	if _, err := UnmarshalLanIDAnnouncement(bad); err == nil {
		t.Fatal("tampered signature should be rejected")
	}

	// 篡改端口（签名覆盖的字段）
	bad2 := append([]byte(nil), wire...)
	bad2[3] ^= 0xFF
	if _, err := UnmarshalLanIDAnnouncement(bad2); err == nil {
		t.Fatal("tampered port should be rejected")
	}

	// 截断
	if _, err := UnmarshalLanIDAnnouncement(wire[:10]); err == nil {
		t.Fatal("truncated announcement should be rejected")
	}
}

func TestLanIDAnnouncementWrongKey(t *testing.T) {
	ident, _ := GenerateIdentityKeyPair()
	other, _ := GenerateIdentityKeyPair()
	ann := &LanIDAnnouncement{
		Version: 1, Platform: PlatformMacOS, Caps: CapDirectLink,
		Port: 24801, Name: "A", PubKey: IdentityPublicKeyBytes(ident),
	}
	// 用另一把密钥签名（通告内公钥是 ident 的）
	wire, _ := ann.Sign(other)
	if _, err := UnmarshalLanIDAnnouncement(wire); err == nil {
		t.Fatal("signature from wrong key should be rejected")
	}
}

func TestLanIDTrustChain(t *testing.T) {
	ident, _ := GenerateIdentityKeyPair()
	peer, _ := GenerateIdentityKeyPair()
	peerID := IdentityPublicKeyBytes(peer)

	chain := NewTrustChain(ident)
	if chain.IsTrusted(peerID) {
		t.Fatal("unknown peer should not be trusted")
	}
	if err := chain.Append(peerID, 0x00); err != nil {
		t.Fatalf("Append trust: %v", err)
	}
	if !chain.IsTrusted(peerID) {
		t.Fatal("peer should be trusted after trust record")
	}
	if err := chain.Verify(); err != nil {
		t.Fatalf("Verify: %v", err)
	}

	// 追加 reject 后不再信任
	if err := chain.Append(peerID, 0x01); err != nil {
		t.Fatalf("Append reject: %v", err)
	}
	if chain.IsTrusted(peerID) {
		t.Fatal("peer should not be trusted after reject record")
	}
	if err := chain.Verify(); err != nil {
		t.Fatalf("Verify after reject: %v", err)
	}
	if len(chain.Records()) != 2 {
		t.Fatalf("expected 2 records, got %d", len(chain.Records()))
	}
}

func TestLanIDTrustChainTampered(t *testing.T) {
	ident, _ := GenerateIdentityKeyPair()
	peer, _ := GenerateIdentityKeyPair()
	peerID := IdentityPublicKeyBytes(peer)

	chain := NewTrustChain(ident)
	_ = chain.Append(peerID, 0x00)
	_ = chain.Append(peerID, 0x00)

	// 篡改第一条记录的 Decision（trust → reject），PrevHash 链应断裂
	records := chain.Records()
	records[0].Decision = 0x01
	// 直接改内部状态以模拟存储被篡改
	chain.mu.Lock()
	chain.records[0].Decision = 0x01
	chain.mu.Unlock()

	if err := chain.Verify(); err == nil {
		t.Fatal("tampered record should break chain verification")
	}
}

func TestLanIDFingerprint(t *testing.T) {
	ident, _ := GenerateIdentityKeyPair()
	pub := IdentityPublicKeyBytes(ident)
	fp := PublicKeyFingerprintFromBytes(pub)
	if len(fp) != 32 {
		t.Fatalf("fingerprint should be 32 hex chars, got %d", len(fp))
	}
	// 与 crypto.go 的 PublicKeyFingerprint 口径一致（同一公钥字节）
	if fp != PublicKeyFingerprintFromBytes(pub) {
		t.Fatal("fingerprint not deterministic")
	}
}
