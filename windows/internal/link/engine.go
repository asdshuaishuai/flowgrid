// 连接引擎：TCP/TLS 监听与拨号、信任决策、重连与状态注册表。
//
// 与发现层解耦：地址经注入的 Resolver 从发现表解析（specs/windows-link）。
// 连接键 = 对端证书指纹（TLS 握手后即知、稳定唯一）；快照里的 Instance
// 由对端名派生（IDENTIFY 名 == mDNS TXT name，两侧配置一致）。
package link

import (
	"crypto/tls"
	"crypto/x509"
	"errors"
	"fmt"
	"log/slog"
	"math/rand"
	"net"
	"sort"
	"strconv"
	"sync"
	"sync/atomic"
	"time"

	"github.com/flowgrid/protocol"
)

const maxReconnectAttempts = 5 // §10.1

type Config struct {
	Name       string
	Port       int
	AutoAccept bool
	Platform   protocol.Platform
	Caps       protocol.CapabilityFlags
}

// Resolver 把发现层的实例名解析成地址（fgcore 用 mDNS 发现表实现）。
type Resolver func(instance string) (ip net.IP, port int, ok bool)

type SelfInfo struct {
	Name        string `json:"name"`
	Instance    string `json:"instance"`
	Fingerprint string `json:"fingerprint"`
	Port        int    `json:"port"`
	Platform    string `json:"platform"`
}

type PairPending struct {
	ID          string `json:"id"` // = 对端证书指纹
	Name        string `json:"name"`
	Fingerprint string `json:"fingerprint"`
	Addr        string `json:"addr"`
}

type desiredPeer struct {
	ip   net.IP
	port int
	pin  *x509.Certificate // 固定根：已配对对端的证书
}

type Engine struct {
	cfg     Config
	id      *Identity
	trust   *TrustStore
	resolve Resolver

	// 会话事件回调（main 装配时设置一次，link 内部只读）。
	onPeerFrame    func(peer string, f *protocol.HIDFrame)
	onClipboard    func(peer string, c protocol.Clipboard)
	onFileOffer    func(peer string, offer protocol.FileOffer)
	onFileProgress func(peer, dir string, sent, total int)
	onFileSendDone func(peer string, err error)
	onFileComplete func(peer string, offer protocol.FileOffer, data []byte, err error)

	mu         sync.Mutex
	ln         net.Listener
	links      map[string]*session // 指纹 → 会话（含重连占位）
	pending    map[string]*session // 指纹 → 等用户确认的会话
	desired    map[string]desiredPeer
	attempts   map[string]int // 指纹（重连）或 ip:port（首拨）→ 已试次数
	watch      map[int]func()
	nextWatch  int
	closed     bool
	autoAccept atomic.Bool
	wg         sync.WaitGroup
}

func New(cfg Config, id *Identity, trust *TrustStore, resolve Resolver) *Engine {
	e := &Engine{
		cfg:      cfg,
		id:       id,
		trust:    trust,
		resolve:  resolve,
		links:    map[string]*session{},
		pending:  map[string]*session{},
		desired:  map[string]desiredPeer{},
		attempts: map[string]int{},
		watch:    map[int]func(){},
	}
	e.autoAccept.Store(cfg.AutoAccept)
	return e
}

// SetAutoAccept 运行时切换「自动信任并接受未配对对端」（设置屏
// 「新设备需确认」的反面）。仅建议开发/可信内网场景开启。
func (e *Engine) SetAutoAccept(enabled bool) { e.autoAccept.Store(enabled) }

func (e *Engine) AutoAccept() bool { return e.autoAccept.Load() }

// Addr 返回监听地址（Listen 之后有效；测试与自通告用）。
func (e *Engine) Addr() net.Addr {
	e.mu.Lock()
	defer e.mu.Unlock()
	if e.ln == nil {
		return nil
	}
	return e.ln.Addr()
}

// Listen 开始监听 TCP 数据端口（§7.2 DirectLink，默认 24801）。
func (e *Engine) Listen() error {
	ln, err := tls.Listen("tcp", fmt.Sprintf(":%d", e.cfg.Port), e.tlsConfigServer())
	if err != nil {
		return fmt.Errorf("link: listen tcp %d: %w", e.cfg.Port, err)
	}
	e.mu.Lock()
	e.ln = ln
	e.mu.Unlock()
	e.wg.Add(1)
	go e.acceptLoop(ln)
	return nil
}

// tlsConfigServer 监听方配置。
//
// SAFETY（design.md D3/D4）：ClientAuth 用 RequireAnyClientCert——要求对端
// 出示证书以便按指纹识别（不验 CA 链，链上信任由 IDENTIFY 确认门把关）；
// 两侧指纹对称后，重连查重与信任库才有一致的连接键。
func (e *Engine) tlsConfigServer() *tls.Config {
	return &tls.Config{
		Certificates: []tls.Certificate{e.id.Cert},
		MinVersion:   tls.VersionTLS13,
		ClientAuth:   tls.RequireAnyClientCert,
		VerifyConnection: func(cs tls.ConnectionState) error {
			if len(cs.PeerCertificates) == 0 {
				return nil // 未请求客户端证书：空是正常路径
			}
			return checkPeerCert(cs)
		},
	}
}

// tlsConfigClient 拨号方配置：固定根钉扎（pinned roots）。
//
// SAFETY（design.md D3/D4）：把已配对对端的证书放进 RootCAs，走 Go 标准
// 链校验——只有「确系那台对端」的证书能通过，全程无任何校验旁路。证书
// 来自信任库（用户确认配对时持久化）；未配对对端在 Connect 处直接拒绝。
func (e *Engine) tlsConfigClient(pin *x509.Certificate) *tls.Config {
	pool := x509.NewCertPool()
	pool.AddCert(pin)
	return &tls.Config{
		Certificates: []tls.Certificate{e.id.Cert},
		MinVersion:   tls.VersionTLS13,
		ServerName:   "flowgrid.local",
		RootCAs:      pool,
	}
}

func checkPeerCert(cs tls.ConnectionState) error {
	if len(cs.PeerCertificates) == 0 {
		return errors.New("link: peer presented no certificate")
	}
	leaf := cs.PeerCertificates[0]
	if leaf.PublicKeyAlgorithm == x509.UnknownPublicKeyAlgorithm {
		return errors.New("link: peer certificate has no usable public key")
	}
	return nil
}

func (e *Engine) acceptLoop(ln net.Listener) {
	defer e.wg.Done()
	for {
		nc, err := ln.Accept()
		if err != nil {
			return
		}
		e.wg.Add(1)
		go func() {
			defer e.wg.Done()
			e.adopt(nc, true)
		}()
	}
}

// adopt 收养一条监听方连接：TLS 握手 → 指纹查重 → 交给会话生命周期。
func (e *Engine) adopt(nc net.Conn, inbound bool) {
	conn, ok := nc.(*tls.Conn)
	if !ok {
		_ = nc.Close()
		return
	}
	_ = conn.SetDeadline(time.Now().Add(handshakeDeadline))
	if err := conn.Handshake(); err != nil {
		slog.Warn("link: inbound tls handshake failed", "remote", conn.RemoteAddr(), "err", err)
		_ = conn.Close()
		return
	}
	cs := conn.ConnectionState()
	if len(cs.PeerCertificates) == 0 {
		slog.Warn("link: inbound session without peer certificate", "remote", conn.RemoteAddr())
		_ = conn.Close()
		return
	}
	fp := Fingerprint(cs.PeerCertificates[0].Raw)
	e.mu.Lock()
	if e.closed || e.links[fp] != nil {
		e.mu.Unlock()
		_ = conn.Close()
		return
	}
	s := newSession(e, conn, fp, cs.PeerCertificates[0].Raw, inbound)
	e.links[fp] = s
	e.mu.Unlock()
	slog.Info("link: inbound session up", "fingerprint", fp, "remote", conn.RemoteAddr())
	e.notify()
	s.run()
}

// Connect 按实例名解析地址并发起连接。拨号需要钉扎的对端证书（固定根），
// 未配对（信任库无此对端证书）时拒绝——首次配对的证书分发机制见
// PROTOCOL-ISSUES P3，属协议规范空缺。
func (e *Engine) Connect(instance string) error {
	ip, port, ok := e.resolve(instance)
	if !ok {
		return fmt.Errorf("link: unknown instance %q", instance)
	}
	pinDER := e.trust.CertByName(NameFromInstance(instance))
	if pinDER == nil {
		return fmt.Errorf("link: peer %q is not paired (no pinned certificate)", instance)
	}
	pin, err := x509.ParseCertificate(pinDER)
	if err != nil {
		return fmt.Errorf("link: pinned certificate corrupt: %w", err)
	}
	go e.dial(ip, port, pin)
	return nil
}

func (e *Engine) dial(ip net.IP, port int, pin *x509.Certificate) {
	addr := net.JoinHostPort(ip.String(), strconv.Itoa(port))
	raw, err := net.DialTimeout("tcp", addr, 5*time.Second)
	if err != nil {
		slog.Warn("link: dial failed", "addr", addr, "err", err)
		e.redialByAddr(ip, port, pin)
		return
	}
	conn := tls.Client(raw, e.tlsConfigClient(pin))
	_ = conn.SetDeadline(time.Now().Add(handshakeDeadline))
	if err := conn.Handshake(); err != nil {
		slog.Warn("link: outbound tls handshake failed", "addr", addr, "err", err)
		_ = raw.Close()
		e.redialByAddr(ip, port, pin)
		return
	}
	if len(conn.ConnectionState().PeerCertificates) == 0 {
		_ = conn.Close()
		return
	}
	fp := Fingerprint(conn.ConnectionState().PeerCertificates[0].Raw)

	e.mu.Lock()
	if e.closed {
		e.mu.Unlock()
		_ = conn.Close()
		return
	}
	if old := e.links[fp]; old != nil {
		e.mu.Unlock()
		_ = conn.Close() // 已有连接（或重连占位），不要第二条
		return
	}
	e.desired[fp] = desiredPeer{ip: ip, port: port, pin: pin}
	delete(e.attempts, fp)
	s := newSession(e, conn, fp, conn.ConnectionState().PeerCertificates[0].Raw, false)
	e.links[fp] = s
	e.mu.Unlock()
	slog.Info("link: outbound session up", "fingerprint", fp, "remote", addr)
	e.notify()
	s.run()
}

// redialByAddr 首拨失败的重试：此时还没有指纹，按地址计数。
func (e *Engine) redialByAddr(ip net.IP, port int, pin *x509.Certificate) {
	key := net.JoinHostPort(ip.String(), strconv.Itoa(port))
	e.mu.Lock()
	n := e.attempts[key] + 1
	e.attempts[key] = n
	e.mu.Unlock()
	if n > maxReconnectAttempts {
		return
	}
	time.AfterFunc(reconnectBackoff(n), func() { e.dial(ip, port, pin) })
}

// sessionEnded 会话收尾：用户/对端主动断 → 移除并放弃期望；否则按 §10.1 重连。
func (e *Engine) sessionEnded(s *session) {
	e.mu.Lock()
	if cur := e.links[s.fingerprint]; cur == s {
		final := s.userClose || s.remoteFinal
		_, want := e.desired[s.fingerprint]
		switch {
		case final:
			delete(e.links, s.fingerprint)
			delete(e.desired, s.fingerprint)
		case want:
			s.mu.Lock()
			s.state = StateReconnecting
			s.mu.Unlock() // 重连占位留在表里，UI 可见
		default:
			delete(e.links, s.fingerprint)
			s.mu.Lock()
			s.state = StateOffline
			s.mu.Unlock()
		}
	}
	e.mu.Unlock()

	s.mu.Lock()
	slog.Info("link: session ended", "peer", s.peerName, "state", s.state,
		"reason", s.closeReason)
	s.mu.Unlock()

	e.notify()
	e.maybeReconnect(s)
}

func (e *Engine) maybeReconnect(s *session) {
	s.mu.Lock()
	final := s.userClose || s.remoteFinal
	s.mu.Unlock()
	if final {
		return
	}
	e.mu.Lock()
	dp, want := e.desired[s.fingerprint]
	if !want {
		e.mu.Unlock()
		return
	}
	e.attempts[s.fingerprint]++
	n := e.attempts[s.fingerprint]
	e.mu.Unlock()
	if n > maxReconnectAttempts {
		return // §10.1：超过最大次数 → idle（离线态）
	}
	time.AfterFunc(reconnectBackoff(n), func() { e.dial(dp.ip, dp.port, dp.pin) })
}

// reconnectBackoff §10.1：200ms 起倍增，±20% 抖动，上限 5s。
func reconnectBackoff(attempt int) time.Duration {
	base := 200 * time.Millisecond << uint(attempt-1)
	if base > 5*time.Second {
		base = 5 * time.Second
	}
	return time.Duration(float64(base) * (0.8 + 0.4*rand.Float64()))
}

// ── 信任决策（specs/windows-link「身份与信任」） ─────────────────────────

// decidePeer 在 IDENTIFY 处理里被调用：已信任/自动信任 → 0x00；否则登记
// pending 并回 0x03，由 UI 经 ResolvePair 裁决，30s 超时按拒绝（session 侧）。
func (e *Engine) decidePeer(s *session) uint8 {
	if e.autoAccept.Load() {
		_ = e.trust.Add(s.fingerprint, s.peerName, s.peerCertDER)
		return protocol.IdentifyAccepted
	}
	if e.trust.IsTrusted(s.fingerprint) {
		return protocol.IdentifyAccepted
	}
	e.mu.Lock()
	if _, exists := e.pending[s.fingerprint]; !exists {
		e.pending[s.fingerprint] = s
	}
	e.mu.Unlock()
	e.notify()
	return protocol.IdentifyNeedConfirm
}

func (e *Engine) resolvePending(s *session) {
	e.mu.Lock()
	delete(e.pending, s.fingerprint)
	e.mu.Unlock()
	e.notify()
}

// ResolvePair 对 pending 的用户裁决；返回 false 表示已不存在（超时/消失）。
func (e *Engine) ResolvePair(id string, accept bool) bool {
	e.mu.Lock()
	s := e.pending[id]
	e.mu.Unlock()
	if s == nil {
		return false
	}
	status := uint8(protocol.IdentifyRejected)
	if accept {
		status = protocol.IdentifyAccepted
	}
	select {
	case s.confirmCh <- status:
		return true
	default:
		return false
	}
}

// ── 查询与订阅 ──────────────────────────────────────────────────────────

// SetHandlers 设置会话事件回调（main 启动时一次性调用）。
func (e *Engine) SetHandlers(
	onPeerFrame func(peer string, f *protocol.HIDFrame),
	onClipboard func(peer string, c protocol.Clipboard),
	onFileOffer func(peer string, offer protocol.FileOffer),
	onFileProgress func(peer, dir string, sent, total int),
	onFileSendDone func(peer string, err error),
	onFileComplete func(peer string, offer protocol.FileOffer, data []byte, err error),
) {
	e.onPeerFrame = onPeerFrame
	e.onClipboard = onClipboard
	e.onFileOffer = onFileOffer
	e.onFileProgress = onFileProgress
	e.onFileSendDone = onFileSendDone
	e.onFileComplete = onFileComplete
}

// PeerPlatform 返回在线对端的平台（键位映射 §6.3 需要）。
func (e *Engine) PeerPlatform(peer string) protocol.Platform {
	if s := e.sessionByName(peer); s != nil {
		s.mu.Lock()
		defer s.mu.Unlock()
		if s.peerPlatform != 0 {
			return s.peerPlatform
		}
	}
	return e.cfg.Platform
}

// sessionByName 按 IDENTIFY 名找会话（在线快照同名）。
func (e *Engine) sessionByName(peer string) *session {
	e.mu.Lock()
	defer e.mu.Unlock()
	for _, s := range e.links {
		s.mu.Lock()
		match := s.peerName == peer && s.state == StateOnline
		s.mu.Unlock()
		if match {
			return s
		}
	}
	return nil
}

// SendToPeer 给在线对端发一帧（键鼠/剪贴板以外的直发通道走专用方法）。
func (e *Engine) SendToPeer(peer string, frameType uint8, payload []byte) error {
	s := e.sessionByName(peer)
	if s == nil {
		return fmt.Errorf("link: peer %q is not online", peer)
	}
	return s.stream.Emit(frameType, payload)
}

// SendClipboardTo 给在线对端发送剪贴板。
func (e *Engine) SendClipboardTo(peer, mime string, data []byte) error {
	s := e.sessionByName(peer)
	if s == nil {
		return fmt.Errorf("link: peer %q is not online", peer)
	}
	return s.SendClipboard(mime, data)
}

// BroadcastClipboard 把本地剪贴板广播给全部在线对端。
func (e *Engine) BroadcastClipboard(mime string, data []byte) {
	e.mu.Lock()
	sessions := make([]*session, 0, len(e.links))
	for _, s := range e.links {
		s.mu.Lock()
		online := s.state == StateOnline
		s.mu.Unlock()
		if online {
			sessions = append(sessions, s)
		}
	}
	e.mu.Unlock()
	for _, s := range sessions {
		_ = s.SendClipboard(mime, data)
	}
}

// SendFileToPeer 给在线对端发起文件传输（§7.4）。
func (e *Engine) SendFileToPeer(peer, name string, data []byte) error {
	s := e.sessionByName(peer)
	if s == nil {
		return fmt.Errorf("link: peer %q is not online", peer)
	}
	return s.SendFile(name, data)
}

// OnlinePeerNames 返回在线对端名，按实例名稳定排序（切换热键 1..8 的序）。
func (e *Engine) OnlinePeerNames() []string {
	names := make([]string, 0, len(e.links))
	for _, snap := range e.Snapshots() {
		if snap.State == string(StateOnline) {
			names = append(names, snap.Name)
		}
	}
	return names
}

func (e *Engine) SelfInfo() SelfInfo {
	return SelfInfo{
		Name:        e.cfg.Name,
		Instance:    InstanceOf(e.cfg.Name),
		Fingerprint: e.id.Fingerprint,
		Port:        e.cfg.Port,
		Platform:    e.cfg.Platform.String(),
	}
}

func (e *Engine) Snapshots() []LinkSnapshot {
	e.mu.Lock()
	defer e.mu.Unlock()
	out := make([]LinkSnapshot, 0, len(e.links))
	for _, s := range e.links {
		s.mu.Lock()
		out = append(out, s.snapshotLocked())
		s.mu.Unlock()
	}
	sort.Slice(out, func(i, j int) bool { return out[i].Instance < out[j].Instance })
	return out
}

func (e *Engine) PendingPairs() []PairPending {
	e.mu.Lock()
	defer e.mu.Unlock()
	out := make([]PairPending, 0, len(e.pending))
	for _, s := range e.pending {
		s.mu.Lock()
		out = append(out, PairPending{
			ID:          s.fingerprint,
			Name:        s.peerName,
			Fingerprint: s.fingerprint,
			Addr:        s.remoteAddr.String(),
		})
		s.mu.Unlock()
	}
	sort.Slice(out, func(i, j int) bool { return out[i].ID < out[j].ID })
	return out
}

// TrustEntries 已授权设备列表（设置屏管理）。
func (e *Engine) TrustEntries() []TrustView { return e.trust.Entries() }

// TrustRevoke 撤销一条已授权设备。
func (e *Engine) TrustRevoke(fingerprint string) error { return e.trust.Remove(fingerprint) }

// Disconnect 按实例名断开（用户语义：不再自动重连）。
func (e *Engine) Disconnect(instance string) bool {
	e.mu.Lock()
	var target *session
	for _, s := range e.links {
		s.mu.Lock()
		match := InstanceOf(s.peerName) == instance
		s.mu.Unlock()
		if match {
			target = s
			break
		}
	}
	e.mu.Unlock()
	if target == nil {
		return false
	}
	target.Disconnect()
	return true
}

// OnChange 订阅任意连接/确认状态变化，返回取消订阅函数。
func (e *Engine) OnChange(fn func()) (cancel func()) {
	e.mu.Lock()
	defer e.mu.Unlock()
	id := e.nextWatch
	e.nextWatch++
	e.watch[id] = fn
	return func() {
		e.mu.Lock()
		defer e.mu.Unlock()
		delete(e.watch, id)
	}
}

func (e *Engine) notify() {
	e.mu.Lock()
	fns := make([]func(), 0, len(e.watch))
	for _, f := range e.watch {
		fns = append(fns, f)
	}
	e.mu.Unlock()
	for _, f := range fns {
		f()
	}
}

func (e *Engine) Stop() {
	e.mu.Lock()
	if e.closed {
		e.mu.Unlock()
		return
	}
	e.closed = true
	ln := e.ln
	sessions := make([]*session, 0, len(e.links))
	for _, s := range e.links {
		sessions = append(sessions, s)
	}
	e.mu.Unlock()
	if ln != nil {
		_ = ln.Close()
	}
	for _, s := range sessions {
		s.teardown("engine stop")
	}
}
