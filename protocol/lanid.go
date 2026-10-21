// 智能连接的 LAN 私有链发现（PROTOCOL.md §3.1）。
//
// 去中心服务：每设备自带 Announcer（周期多播签名通告）与 Listener（收通告
// 验签聚合）。识别 = 验签通过；信任 = 哈希链式签名日志（私有链）。
// 数据通道走 TCP（§7.2），本文件只管发现与识别层，不碰传输。
//
// 语义与 mdns.go 一致：Go 侧缓存、C 侧轮询（Poll 取走即出队）。
package protocol

import (
	"crypto/ecdsa"
	"crypto/elliptic"
	"crypto/rand"
	"crypto/sha256"
	"crypto/x509"
	"encoding/binary"
	"encoding/hex"
	"errors"
	"fmt"
	"math/big"
	"net"
	"sync"
	"time"
)

// 多播参数（§3.1.1）。
const (
	LanIDGroup          = "239.255.42.99"
	LanIDPort           = 24801
	LanIDAnnouncePeriod = 2 * time.Second
	LanIDPeerMaxAge     = 10 * time.Second
	LanIDMaxNameLen     = 64
	lanIDSigSize        = 64 // ECDSA P-256 r||s
)

var (
	errLanIDTooShort    = errors.New("protocol: lanid announcement too short")
	errLanIDNameTooLong = errors.New("protocol: lanid name too long")
	errLanIDBadPubKey   = errors.New("protocol: lanid invalid public key")
	errLanIDBadSig      = errors.New("protocol: lanid signature verification failed")
)

// ── 身份密钥（ECDSA P-256，持久；与 §4.3 的临时 ECDH 密钥分离） ──────────

// GenerateIdentityKeyPair 生成持久身份密钥对（签名用）。
func GenerateIdentityKeyPair() (*ecdsa.PrivateKey, error) {
	return ecdsa.GenerateKey(elliptic.P256(), rand.Reader)
}

// ExportIdentityPrivateKey 序列化身份私钥（SEC1 EC，客户端负责安全存储）。
func ExportIdentityPrivateKey(priv *ecdsa.PrivateKey) ([]byte, error) {
	return x509.MarshalECPrivateKey(priv)
}

// ImportIdentityPrivateKey 反序列化身份私钥。
func ImportIdentityPrivateKey(data []byte) (*ecdsa.PrivateKey, error) {
	return x509.ParseECPrivateKey(data)
}

// IdentityPublicKeyBytes 导出身份公钥（65 字节 SEC1 非压缩点）。
func IdentityPublicKeyBytes(priv *ecdsa.PrivateKey) []byte {
	return elliptic.Marshal(elliptic.P256(), priv.PublicKey.X, priv.PublicKey.Y)
}

// parseECDSAPublicKey 解析 65 字节 SEC1 非压缩点为 ECDSA 公钥。
func parseECDSAPublicKey(data []byte) (*ecdsa.PublicKey, error) {
	if len(data) != PublicKeySize {
		return nil, fmt.Errorf("%w: must be %d bytes, got %d", errLanIDBadPubKey, PublicKeySize, len(data))
	}
	x, y := elliptic.Unmarshal(elliptic.P256(), data)
	if x == nil {
		return nil, errLanIDBadPubKey
	}
	return &ecdsa.PublicKey{Curve: elliptic.P256(), X: x, Y: y}, nil
}

// signP256 用私钥对 digest 签名，返回固定 64 字节（r||s）。
func signP256(priv *ecdsa.PrivateKey, digest []byte) ([]byte, error) {
	r, s, err := ecdsa.Sign(rand.Reader, priv, digest)
	if err != nil {
		return nil, fmt.Errorf("protocol: lanid sign: %w", err)
	}
	sig := make([]byte, lanIDSigSize)
	r.FillBytes(sig[:32])
	s.FillBytes(sig[32:])
	return sig, nil
}

// verifyP256 验签（固定 64 字节 r||s）。
func verifyP256(pub *ecdsa.PublicKey, digest, sig []byte) bool {
	if len(sig) != lanIDSigSize {
		return false
	}
	r := new(big.Int).SetBytes(sig[:32])
	s := new(big.Int).SetBytes(sig[32:])
	return ecdsa.Verify(pub, digest, r, s)
}

// ── 通告记录（§3.1.1） ──────────────────────────────────────────────────

// LanIDAnnouncement 多播通告记录（不含签名的字段）。
type LanIDAnnouncement struct {
	Version  uint8
	Platform Platform
	Caps     CapabilityFlags
	Port     uint16
	Name     string
	PubKey   []byte // 65 字节 SEC1
}

// marshalUnsigned 编码记录（Version..PubKey，不含签名）。
func (a *LanIDAnnouncement) marshalUnsigned() []byte {
	nameBytes := []byte(a.Name)
	buf := make([]byte, 0, 6+len(nameBytes)+PublicKeySize)
	buf = append(buf, a.Version, byte(a.Platform), byte(a.Caps))
	var port [2]byte
	binary.BigEndian.PutUint16(port[:], a.Port)
	buf = append(buf, port[:]...)
	buf = append(buf, byte(len(nameBytes)))
	buf = append(buf, nameBytes...)
	buf = append(buf, a.PubKey...)
	return buf
}

// Sign 用身份私钥签名，返回完整通告（记录 + 64 字节签名）。
func (a *LanIDAnnouncement) Sign(priv *ecdsa.PrivateKey) ([]byte, error) {
	unsigned := a.marshalUnsigned()
	digest := sha256.Sum256(unsigned)
	sig, err := signP256(priv, digest[:])
	if err != nil {
		return nil, err
	}
	return append(unsigned, sig...), nil
}

// UnmarshalLanIDAnnouncement 解析并验签通告。验签失败返回错误（调用方静默丢弃）。
func UnmarshalLanIDAnnouncement(data []byte) (*LanIDAnnouncement, error) {
	if len(data) < 6+PublicKeySize+lanIDSigSize {
		return nil, errLanIDTooShort
	}
	nameLen := int(data[5])
	if nameLen > LanIDMaxNameLen {
		return nil, errLanIDNameTooLong
	}
	unsignedLen := 6 + nameLen + PublicKeySize
	if len(data) < unsignedLen+lanIDSigSize {
		return nil, errLanIDTooShort
	}
	pubKey := data[6+nameLen : unsignedLen]
	sig := data[unsignedLen : unsignedLen+lanIDSigSize]

	pub, err := parseECDSAPublicKey(pubKey)
	if err != nil {
		return nil, err
	}
	digest := sha256.Sum256(data[:unsignedLen])
	if !verifyP256(pub, digest[:], sig) {
		return nil, errLanIDBadSig
	}
	return &LanIDAnnouncement{
		Version:  data[0],
		Platform: Platform(data[1]),
		Caps:     CapabilityFlags(data[2]),
		Port:     binary.BigEndian.Uint16(data[3:5]),
		Name:     string(data[6 : 6+nameLen]),
		PubKey:   pubKey,
	}, nil
}

// ── Listener（收通告验签聚合，镜像 MdnsBrowser） ─────────────────────────

// LanIDPeer 识别到的对端（身份公钥即稳定标识）。
type LanIDPeer struct {
	Name        string
	Platform    Platform
	Caps        CapabilityFlags
	Addr        net.IP
	Port        uint16
	PubKey      []byte
	Fingerprint string // hex(SHA-256(PubKey)[0:16])
	FirstSeen   time.Time
	LastSeen    time.Time
}

// Key 稳定标识：身份公钥的 hex。
func (p LanIDPeer) Key() string {
	return hex.EncodeToString(p.PubKey)
}

// LanIDListener 聚合验签通过的通告（纯逻辑：客户端持有 UDP socket，喂字节进来）。
type LanIDListener struct {
	mu      sync.Mutex
	peers   map[string]LanIDPeer
	pending []LanIDPeer
}

// NewLanIDListener 创建监听器。
func NewLanIDListener() *LanIDListener {
	return &LanIDListener{peers: map[string]LanIDPeer{}}
}

// Feed 喂入一条收到的通告字节与源 IP；验签通过则聚合，失败静默丢弃。
func (l *LanIDListener) Feed(data []byte, from net.IP) {
	ann, err := UnmarshalLanIDAnnouncement(data)
	if err != nil {
		return // 验签失败/畸形：静默丢弃（网络噪声）
	}
	now := time.Now()
	peer := LanIDPeer{
		Name:        ann.Name,
		Platform:    ann.Platform,
		Caps:        ann.Caps,
		Addr:        from,
		Port:        ann.Port,
		PubKey:      ann.PubKey,
		Fingerprint: PublicKeyFingerprintFromBytes(ann.PubKey),
		FirstSeen:   now,
		LastSeen:    now,
	}
	l.mu.Lock()
	defer l.mu.Unlock()
	if existing, ok := l.peers[peer.Key()]; ok {
		peer.FirstSeen = existing.FirstSeen
	}
	l.peers[peer.Key()] = peer
	l.pending = append(l.pending, peer)
}

// Poll 取走自上次调用以来新发现/更新的对端（取走即出队）。
func (l *LanIDListener) Poll() []LanIDPeer {
	l.mu.Lock()
	defer l.mu.Unlock()
	out := l.pending
	l.pending = nil
	return out
}

// Peers 返回当前已知对端快照。
func (l *LanIDListener) Peers() []LanIDPeer {
	l.mu.Lock()
	defer l.mu.Unlock()
	out := make([]LanIDPeer, 0, len(l.peers))
	for _, p := range l.peers {
		out = append(out, p)
	}
	return out
}

// Prune 丢掉超过 maxAge 没再出现的对端，返回淘汰数量。
func (l *LanIDListener) Prune(maxAge time.Duration) int {
	l.mu.Lock()
	defer l.mu.Unlock()
	n := 0
	for k, p := range l.peers {
		if time.Since(p.LastSeen) > maxAge {
			delete(l.peers, k)
			n++
		}
	}
	return n
}

// ── Announcer（签名通告，纯逻辑：客户端持有 UDP socket 并周期发送） ─────────

// LanIDAnnouncer 产生签名通告字节（一次性）；客户端按周期调用 Announce 并发送。
type LanIDAnnouncer struct {
	mu       sync.Mutex
	ident    *ecdsa.PrivateKey
	announce *LanIDAnnouncement
}

// NewLanIDAnnouncer 创建通告器。
func NewLanIDAnnouncer(ident *ecdsa.PrivateKey, platform Platform, caps CapabilityFlags, port uint16, name string) (*LanIDAnnouncer, error) {
	if len(name) > LanIDMaxNameLen {
		return nil, errLanIDNameTooLong
	}
	return &LanIDAnnouncer{
		ident: ident,
		announce: &LanIDAnnouncement{
			Version:  1,
			Platform: platform,
			Caps:     caps,
			Port:     port,
			Name:     name,
			PubKey:   IdentityPublicKeyBytes(ident),
		},
	}, nil
}

// Announce 产生一条签名通告字节（客户端经 UDP 多播发送）。
func (a *LanIDAnnouncer) Announce() ([]byte, error) {
	a.mu.Lock()
	ann := a.announce
	ident := a.ident
	a.mu.Unlock()
	if ann == nil || ident == nil {
		return nil, errors.New("protocol: lanid announcer not initialized")
	}
	return ann.Sign(ident)
}

// ── 私有链信任日志（§3.1.3，局域网私有区块链） ───────────────────────────

// TrustRecord 哈希链式签名信任记录。
type TrustRecord struct {
	PeerID      []byte   // 对端身份公钥 (65)
	Fingerprint [16]byte // hex(SHA-256(PeerID)[0:16]) 的原始字节
	Decision    uint8    // 0x00 = trust, 0x01 = reject
	Timestamp   uint64   // Unix 秒
	PrevHash    [32]byte // 前一条记录的 SHA-256（首条为全零）
	Signature   []byte   // 64 字节
}

// marshalUnsigned 编码记录（除签名外全部字段）。
func (r *TrustRecord) marshalUnsigned() []byte {
	buf := make([]byte, 0, PublicKeySize+16+1+8+32)
	buf = append(buf, r.PeerID...)
	buf = append(buf, r.Fingerprint[:]...)
	buf = append(buf, r.Decision)
	var ts [8]byte
	binary.BigEndian.PutUint64(ts[:], r.Timestamp)
	buf = append(buf, ts[:]...)
	buf = append(buf, r.PrevHash[:]...)
	return buf
}

// hash 返回记录的 SHA-256（供下一条 PrevHash 链接）。
func (r *TrustRecord) hash() [32]byte {
	return sha256.Sum256(r.marshalUnsigned())
}

// TrustChain 每设备本地的哈希链式签名信任日志（去中心，无需共识）。
type TrustChain struct {
	mu      sync.Mutex
	records []TrustRecord
	ident   *ecdsa.PrivateKey
}

// NewTrustChain 用身份私钥创建信任链。
func NewTrustChain(ident *ecdsa.PrivateKey) *TrustChain {
	return &TrustChain{ident: ident}
}

// Append 追加一条信任记录（自动链接 PrevHash 并签名）。
func (c *TrustChain) Append(peerID []byte, decision uint8) error {
	c.mu.Lock()
	defer c.mu.Unlock()
	if c.ident == nil {
		return errors.New("protocol: trust chain has no identity key")
	}
	if len(peerID) != PublicKeySize {
		return fmt.Errorf("%w: peer id must be %d bytes", errLanIDBadPubKey, PublicKeySize)
	}
	rec := TrustRecord{
		PeerID:    append([]byte(nil), peerID...),
		Decision:  decision,
		Timestamp: uint64(time.Now().Unix()),
	}
	sum := sha256.Sum256(peerID)
	copy(rec.Fingerprint[:], sum[:16])
	if len(c.records) > 0 {
		rec.PrevHash = c.records[len(c.records)-1].hash()
	}
	digest := sha256.Sum256(rec.marshalUnsigned())
	sig, err := signP256(c.ident, digest[:])
	if err != nil {
		return err
	}
	rec.Signature = sig
	c.records = append(c.records, rec)
	return nil
}

// IsTrusted 对端是否被信任（链上最新一条决策为 trust）。
func (c *TrustChain) IsTrusted(peerID []byte) bool {
	c.mu.Lock()
	defer c.mu.Unlock()
	key := hex.EncodeToString(peerID)
	for i := len(c.records) - 1; i >= 0; i-- {
		if hex.EncodeToString(c.records[i].PeerID) == key {
			return c.records[i].Decision == 0x00
		}
	}
	return false
}

// Verify 校验整条链：PrevHash 链接 + 每条记录由本机身份密钥签名。任一断裂返回错误。
func (c *TrustChain) Verify() error {
	c.mu.Lock()
	defer c.mu.Unlock()
	if c.ident == nil {
		return errors.New("protocol: trust chain has no identity key")
	}
	pub := &c.ident.PublicKey
	var prev [32]byte
	for i, rec := range c.records {
		if rec.PrevHash != prev {
			return fmt.Errorf("protocol: trust chain broken at record %d (prevHash mismatch)", i)
		}
		digest := sha256.Sum256(rec.marshalUnsigned())
		if !verifyP256(pub, digest[:], rec.Signature) {
			return fmt.Errorf("protocol: trust chain broken at record %d (bad signature)", i)
		}
		prev = rec.hash()
	}
	return nil
}

// Records 返回链上记录快照。
func (c *TrustChain) Records() []TrustRecord {
	c.mu.Lock()
	defer c.mu.Unlock()
	out := make([]TrustRecord, len(c.records))
	copy(out, c.records)
	return out
}

// PublicKeyFingerprintFromBytes 计算 65 字节公钥的指纹（hex(SHA-256[0:16])）。
func PublicKeyFingerprintFromBytes(pubKey []byte) string {
	sum := sha256.Sum256(pubKey)
	return hex.EncodeToString(sum[:FingerprintSize])
}
