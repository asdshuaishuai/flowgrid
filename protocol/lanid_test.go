package protocol

import (
	"crypto/ecdsa"
	"crypto/elliptic"
	"crypto/rand"
	"crypto/sha256"
	"crypto/x509"
	"encoding/hex"
	"net"
	"testing"
	"time"
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

// ── 加固回归：解析边界、监听聚合、信任链校验 ─────────────────────────

func TestLanIDAnnouncementRejectsBadVersion(t *testing.T) {
	ident, _ := GenerateIdentityKeyPair()
	ann := &LanIDAnnouncement{
		Version: 1, Platform: PlatformLinux, Caps: CapIntelligentLink,
		Port: 24801, Name: "L", PubKey: IdentityPublicKeyBytes(ident),
	}
	wire, _ := ann.Sign(ident)
	wire[0] = 2
	if _, err := UnmarshalLanIDAnnouncement(wire); err == nil {
		t.Fatal("unsupported version should be rejected")
	}
}

func TestLanIDAnnouncementRejectsTrailingBytes(t *testing.T) {
	ident, _ := GenerateIdentityKeyPair()
	ann := &LanIDAnnouncement{
		Version: 1, Platform: PlatformLinux, Caps: CapIntelligentLink,
		Port: 24801, Name: "L", PubKey: IdentityPublicKeyBytes(ident),
	}
	wire, _ := ann.Sign(ident)
	if _, err := UnmarshalLanIDAnnouncement(append(wire, 0x00)); err == nil {
		t.Fatal("trailing bytes should be rejected (not covered by signature)")
	}
}

func TestLanIDAnnouncementMasksReservedCaps(t *testing.T) {
	ident, _ := GenerateIdentityKeyPair()
	ann := &LanIDAnnouncement{
		Version: 1, Platform: PlatformLinux, Caps: CapDirectLink | 0xC0, // bit 6-7 保留
		Port: 24801, Name: "L", PubKey: IdentityPublicKeyBytes(ident),
	}
	wire, _ := ann.Sign(ident)
	got, err := UnmarshalLanIDAnnouncement(wire)
	if err != nil {
		t.Fatalf("Unmarshal: %v", err)
	}
	if got.Caps != CapDirectLink {
		t.Fatalf("reserved caps must be masked, got 0x%02x", byte(got.Caps))
	}
}

func TestImportIdentityRejectsNonP256(t *testing.T) {
	key, err := ecdsa.GenerateKey(elliptic.P384(), rand.Reader)
	if err != nil {
		t.Fatalf("generate P-384: %v", err)
	}
	der, err := x509.MarshalECPrivateKey(key)
	if err != nil {
		t.Fatalf("marshal: %v", err)
	}
	if _, err := ImportIdentityPrivateKey(der); err == nil {
		t.Fatal("P-384 identity must be rejected (wire format is P-256 only)")
	}
}

func TestLanIDListenerAggregatesAndDedupes(t *testing.T) {
	mk := func(name string, port uint16) (*ecdsa.PrivateKey, []byte) {
		ident, _ := GenerateIdentityKeyPair()
		ann := &LanIDAnnouncement{
			Version: 1, Platform: PlatformMacOS, Caps: CapIntelligentLink,
			Port: port, Name: name, PubKey: IdentityPublicKeyBytes(ident),
		}
		wire, _ := ann.Sign(ident)
		return ident, wire
	}
	l := NewLanIDListener()
	_, a := mk("A", 24801)
	_, b := mk("B", 24801)
	l.Feed(a, net.IPv4(192, 168, 1, 10))
	l.Feed(b, net.IPv4(192, 168, 1, 11))

	peers := l.Peers()
	if len(peers) != 2 {
		t.Fatalf("expected 2 peers, got %d", len(peers))
	}
	if len(l.Poll()) != 2 {
		t.Fatalf("first feeds should enqueue 2 updates, got %d", len(l.Poll()))
	}

	// 原样重播（周期通告）：不应再入队。
	l.Feed(a, net.IPv4(192, 168, 1, 10))
	if got := l.Poll(); len(got) != 0 {
		t.Fatalf("unchanged announcement should not enqueue, got %d", len(got))
	}

	// 地址变化属于可见变化，应入队。
	l.Feed(a, net.IPv4(192, 168, 1, 99))
	got := l.Poll()
	if len(got) != 1 || got[0].Addr.String() != "192.168.1.99" {
		t.Fatalf("address change should enqueue one update, got %+v", got)
	}
}

func TestLanIDListenerCopiesCallerBuffer(t *testing.T) {
	ident, _ := GenerateIdentityKeyPair()
	ann := &LanIDAnnouncement{
		Version: 1, Platform: PlatformWindows, Caps: CapIntelligentLink,
		Port: 24801, Name: "W", PubKey: IdentityPublicKeyBytes(ident),
	}
	wire, _ := ann.Sign(ident)
	buf := append([]byte(nil), wire...)
	l := NewLanIDListener()
	l.Feed(buf, net.IPv4(10, 0, 0, 2))
	want := append([]byte(nil), IdentityPublicKeyBytes(ident)...)

	// 模拟 UDP 读循环复用缓冲区。
	for i := range buf {
		buf[i] = 0xAA
	}
	peers := l.Peers()
	if len(peers) != 1 {
		t.Fatalf("expected 1 peer, got %d", len(peers))
	}
	if !bytesEqual(peers[0].PubKey, want) {
		t.Fatal("stored pubkey aliases caller buffer; it must be copied")
	}
}

func TestLanIDListenerPeersStableOrder(t *testing.T) {
	l := NewLanIDListener()
	for i := 0; i < 5; i++ {
		ident, _ := GenerateIdentityKeyPair()
		ann := &LanIDAnnouncement{
			Version: 1, Platform: PlatformLinux, Caps: CapIntelligentLink,
			Port: 24801, Name: "N", PubKey: IdentityPublicKeyBytes(ident),
		}
		wire, _ := ann.Sign(ident)
		l.Feed(wire, net.IPv4(10, 0, 0, byte(i+1)))
	}
	first := l.Peers()
	for i := 0; i < 5; i++ {
		again := l.Peers()
		for j := range first {
			if first[j].Key() != again[j].Key() {
				t.Fatal("Peers() order must be stable (C 侧按 index 取值)")
			}
		}
	}
	for i := 1; i < len(first); i++ {
		if string(first[i-1].PubKey) > string(first[i].PubKey) {
			t.Fatal("Peers() must be sorted by pubkey")
		}
	}
}

func TestLanIDListenerPrune(t *testing.T) {
	ident, _ := GenerateIdentityKeyPair()
	ann := &LanIDAnnouncement{
		Version: 1, Platform: PlatformLinux, Caps: CapIntelligentLink,
		Port: 24801, Name: "P", PubKey: IdentityPublicKeyBytes(ident),
	}
	wire, _ := ann.Sign(ident)
	l := NewLanIDListener()
	l.Feed(wire, net.IPv4(10, 0, 0, 3))
	l.mu.Lock()
	for k, p := range l.peers {
		p.LastSeen = time.Now().Add(-2 * LanIDPeerMaxAge)
		l.peers[k] = p
	}
	l.mu.Unlock()
	if n := l.Prune(LanIDPeerMaxAge); n != 1 {
		t.Fatalf("expected 1 pruned peer, got %d", n)
	}
	if len(l.Peers()) != 0 {
		t.Fatal("pruned peer must be gone")
	}
	if got := l.Poll(); len(got) != 0 {
		t.Fatalf("pruned peer must not resurrect via Poll, got %d", len(got))
	}
}

func TestLanIDTrustChainRejectsBadDecision(t *testing.T) {
	ident, _ := GenerateIdentityKeyPair()
	peer, _ := GenerateIdentityKeyPair()
	chain := NewTrustChain(ident)
	if err := chain.Append(IdentityPublicKeyBytes(peer), 0x02); err == nil {
		t.Fatal("decision other than 0x00/0x01 must be rejected")
	}
}

func TestLanIDTrustChainDetectsFingerprintMismatch(t *testing.T) {
	ident, _ := GenerateIdentityKeyPair()
	peer, _ := GenerateIdentityKeyPair()
	chain := NewTrustChain(ident)
	if err := chain.Append(IdentityPublicKeyBytes(peer), 0x00); err != nil {
		t.Fatalf("Append: %v", err)
	}
	chain.mu.Lock()
	chain.records[0].Fingerprint[0] ^= 0xFF
	chain.mu.Unlock()
	if err := chain.Verify(); err == nil {
		t.Fatal("fingerprint/peerID mismatch must break verification")
	}
}

func TestLanIDUnmarshalMasksReservedCaps(t *testing.T) {
	ident, _ := GenerateIdentityKeyPair()
	name := "L"
	// 手工构造带保留位的通告（Sign 会掩码，这里模拟对端直接写线格式）。
	wire := []byte{1, byte(PlatformLinux), 0xC1, 0x60, 0xE1, byte(len(name))}
	wire = append(wire, name...)
	wire = append(wire, IdentityPublicKeyBytes(ident)...)
	digest := sha256.Sum256(wire)
	sig, err := signP256(ident, digest[:])
	if err != nil {
		t.Fatalf("sign: %v", err)
	}
	got, err := UnmarshalLanIDAnnouncement(append(wire, sig...))
	if err != nil {
		t.Fatalf("Unmarshal: %v", err)
	}
	if got.Caps != CapIntelligentLink {
		t.Fatalf("reserved caps must be masked on parse, got 0x%02x", byte(got.Caps))
	}
}

func TestLanIDAnnouncerRejectsNonP256(t *testing.T) {
	key, err := ecdsa.GenerateKey(elliptic.P384(), rand.Reader)
	if err != nil {
		t.Fatalf("generate P-384: %v", err)
	}
	// IdentityPublicKeyBytes 内部按 P-256 编码；非 P-256 必须在入口拒绝，
	// 否则会 panic 或产出永远验不过的通告。
	if _, err := NewLanIDAnnouncer(key, PlatformLinux, CapIntelligentLink, 24801, "L"); err == nil {
		t.Fatal("P-384 announcer identity must be rejected")
	}
}

func TestTrustChainRejectsNonP256Identity(t *testing.T) {
	key, err := ecdsa.GenerateKey(elliptic.P384(), rand.Reader)
	if err != nil {
		t.Fatalf("generate P-384: %v", err)
	}
	peer, _ := GenerateIdentityKeyPair()
	chain := NewTrustChain(key)
	if err := chain.Append(IdentityPublicKeyBytes(peer), 0x00); err == nil {
		t.Fatal("P-384 trust chain identity must be rejected")
	}
}

func TestTrustChainRecordsIsSnapshot(t *testing.T) {
	ident, _ := GenerateIdentityKeyPair()
	peer, _ := GenerateIdentityKeyPair()
	chain := NewTrustChain(ident)
	if err := chain.Append(IdentityPublicKeyBytes(peer), 0x00); err != nil {
		t.Fatalf("Append: %v", err)
	}
	recs := chain.Records()
	for i := range recs[0].PeerID {
		recs[0].PeerID[i] = 0
	}
	recs[0].Signature[0] ^= 0xFF
	if err := chain.Verify(); err != nil {
		t.Fatalf("mutating Records() snapshot must not affect the chain: %v", err)
	}
}

func bytesEqual(a, b []byte) bool {
	if len(a) != len(b) {
		return false
	}
	for i := range a {
		if a[i] != b[i] {
			return false
		}
	}
	return true
}
