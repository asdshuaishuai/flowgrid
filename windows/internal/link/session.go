// 每对端连接会话（§4.2 IDENTIFY → §9.1 心跳 → 在线）。
//
// 读写各一条 goroutine：写侧 64 帧深出站队列（慢对端溢出即断，与 IPC
// pipeConn 同款防拖死设计），读侧 ParseTCPFrames 分发。帧 HMAC 未启用
// （design.md D2）：DirectLink-TCP 上 TLS 1.3 记录层已是 AEAD 完整性，
// AppendTCPFrame/ParseTCPFrames 传 nil key 即为无 HMAC 记录（§8.3「建议」档）。
package link

import (
	"bufio"
	"crypto/tls"
	"encoding/binary"
	"errors"
	"fmt"
	"log/slog"
	"math/rand"
	"net"
	"sync"
	"time"

	"github.com/flowgrid/protocol"
)

const (
	pairConfirmTimeout = 30 * time.Second
	sendQueueDepth     = 64
	readChunkSize      = 16 * 1024
	handshakeDeadline  = 10 * time.Second
)

type sessionState string

const (
	StateIdentifying    sessionState = "identifying"
	StateAuthenticating sessionState = "authenticating" // 等用户确认对端
	StateOnline         sessionState = "online"
	StateReconnecting   sessionState = "reconnecting"
	StateOffline        sessionState = "offline"
)

// LinkSnapshot 是连接状态的全量快照（specs/windows-link「IPC 与状态可见」）。
type LinkSnapshot struct {
	Name        string  `json:"name"`
	Instance    string  `json:"instance"`
	Addr        string  `json:"addr"`
	Port        int     `json:"port"`
	State       string  `json:"state"`
	LatencyMS   float64 `json:"latency_ms"`
	LatMinMS    float64 `json:"lat_min_ms"`
	LatMaxMS    float64 `json:"lat_max_ms"`
	LatAvgMS    float64 `json:"lat_avg_ms"`
	Samples     int     `json:"samples"`
	Platform    string  `json:"platform"`
	Fingerprint string  `json:"fingerprint"`
	Trusted     bool    `json:"trusted"`
	Inbound     bool    `json:"inbound"`
}

type session struct {
	eng     *Engine
	conn    *tls.Conn
	inbound bool

	// TLS 握手完成后即知的对端属性（证书指纹唯一、稳定，作连接键）。
	fingerprint string
	peerCertDER []byte
	remoteAddr  net.Addr
	remotePort  int

	mu           sync.Mutex
	state        sessionState
	peerName     string
	peerPlatform protocol.Platform
	latencyMS    float64
	sampleCount  int
	closeReason  string
	userClose    bool // 我方 Disconnect() 发起：不重连
	remoteFinal  bool // 对端发了 DISCONNECT：不重连

	send      chan *protocol.HIDFrame
	done      chan struct{}
	closeOnce sync.Once

	mySeq   *protocol.SequenceTracker
	peerSeq *protocol.SequenceTracker
	hb      *protocol.HeartbeatTracker
	lat     *protocol.LatencyMonitor
	stream  *protocol.FrameStream

	// 会话模块（§5.3 剪贴板 / §7.4 文件），emit 接在本会话的流上。
	clip    *protocol.ClipboardSession
	fsender *protocol.FileSender
	frecv   *protocol.FileReceiver

	// 握手推进（读 goroutine 独占，无需锁）
	responded      bool
	responseStatus uint8
	peerAccepted   bool
	tickersStarted bool
	confirmCh      chan uint8
}

func newSession(eng *Engine, conn *tls.Conn, fingerprint string, peerCertDER []byte, inbound bool) *session {
	s := &session{
		eng:         eng,
		conn:        conn,
		inbound:     inbound,
		fingerprint: fingerprint,
		peerCertDER: peerCertDER,
		remoteAddr:  conn.RemoteAddr(),
		state:       StateIdentifying,
		send:        make(chan *protocol.HIDFrame, sendQueueDepth),
		done:        make(chan struct{}),
		confirmCh:   make(chan uint8, 1),
		lat:         protocol.NewLatencyMonitor(64),
	}
	s.remotePort = remotePort(conn.RemoteAddr())
	return s
}

// run 阻塞跑完整个连接生命周期（拨号/监听方各起一条 goroutine 调用）。
func (s *session) run() {
	defer s.eng.sessionEnded(s)

	s.mySeq = protocol.NewSequenceTracker(randSeqInit())
	s.hb = protocol.NewHeartbeatTracker(func() { s.teardown("heartbeat lost") })
	s.stream = protocol.NewFrameStream(s.sink, s.mySeq)

	// 会话模块挂上流；事件经引擎回调冒泡给 hub（输入路由/剪贴板/文件）。
	s.clip = protocol.NewClipboardSession(s.stream.Emit)
	s.clip.OnClipboard = func(c protocol.Clipboard) {
		if cb := s.eng.onClipboard; cb != nil {
			cb(s.peerName, c)
		}
	}
	s.fsender = protocol.NewFileSender(s.stream.Emit)
	s.fsender.OnProgress = func(id uint16, sent, total int) {
		if cb := s.eng.onFileProgress; cb != nil {
			cb(s.peerName, "send", sent, total)
		}
	}
	s.frecv = protocol.NewFileReceiver(s.stream.Emit)
	s.frecv.OnOffer = func(offer protocol.FileOffer) {
		// 入站自动接受（design.md D5）：落盘在 OnComplete。
		_ = s.frecv.Accept(offer.TransferID)
		if cb := s.eng.onFileOffer; cb != nil {
			cb(s.peerName, offer)
		}
	}
	s.frecv.OnComplete = func(offer protocol.FileOffer, data []byte, err error) {
		if cb := s.eng.onFileComplete; cb != nil {
			cb(s.peerName, offer, data, err)
		}
	}
	s.fsender.OnDone = func(id uint16, err error) {
		if cb := s.eng.onFileSendDone; cb != nil {
			cb(s.peerName, err)
		}
	}

	// 握手期墙钟：对端 10s 内没把 IDENTIFY 走完就当死链。
	_ = s.conn.SetDeadline(time.Now().Add(handshakeDeadline))

	s.sendIdentify()
	go s.writeLoop()

	buf := make([]byte, 0, readChunkSize)
	chunk := make([]byte, readChunkSize)
	for {
		n, err := s.conn.Read(chunk)
		if err != nil {
			s.setCloseReason(err.Error())
			return
		}
		buf = append(buf, chunk[:n]...)
		frames, consumed, perr := protocol.ParseTCPFrames(buf, nil)
		buf = append(buf[:0], buf[consumed:]...)
		for _, f := range frames {
			if !s.handleFrame(f) {
				return
			}
		}
		_ = perr // 坏记录已被跳过，流可继续解析
	}
}

// ── 帧处理 ──────────────────────────────────────────────────────────────

func (s *session) handleFrame(f *protocol.HIDFrame) bool {
	if f.Header.FrameType == protocol.FrameIdentify {
		return s.handleIdentify(f)
	}
	if s.peerSeq == nil {
		return true // 对端 IDENTIFY 之前的帧无从校验序列，丢弃
	}
	// §10.3：gap / 重复帧一律静默丢弃，不回 ERROR（防错误风暴）；
	// 但要留痕——间歇性丢帧是定位链路问题的关键线索。
	if err := s.peerSeq.Check(f.Header.Sequence); err != nil {
		slog.Warn("link: frame dropped by sequence check", "peer", s.peerName,
			"type", fmt.Sprintf("0x%02X", f.Header.FrameType),
			"seq", f.Header.Sequence, "err", err)
		return true
	}

	switch f.Header.FrameType {
	case protocol.FrameIdentifyRes:
		return s.handleIdentifyResponse(f)
	case protocol.FrameHeartbeat:
		// 只喂狗、不回帧（design.md D6b）：双侧各自按 2s 周期发送，回显会
		// 形成 1:1 无限放大（实测 36s 内 29.5 万帧的指数风暴）。
		s.hb.ReceivedHeartbeat()
	case protocol.FrameLatencyPing:
		if len(f.Payload) == 8 {
			s.emit(protocol.FrameLatencyPong, f.Payload) // 原样回 T1（§9.2）
		}
	case protocol.FrameLatencyPong:
		if len(f.Payload) == 8 {
			t1 := binary.BigEndian.Uint64(f.Payload)
			if sample, ok := s.lat.RecordPong(t1, nowUs()); ok {
				s.mu.Lock()
				s.latencyMS = sample.ValueMs
				s.sampleCount++
				s.mu.Unlock()
				s.eng.notify()
			} else {
				slog.Debug("link: latency pong rejected", "t1", t1)
			}
		}
	case protocol.FrameDisconnect:
		reason := byte(0xFF)
		if len(f.Payload) == 1 {
			reason = f.Payload[0]
		}
		s.mu.Lock()
		s.remoteFinal = true
		s.closeReason = fmt.Sprintf("对端断开 (0x%02X)", reason)
		s.mu.Unlock()
		return false
	case protocol.FrameClipboard:
		// 重组错误（孤儿块/超时）按协议就地消化，不上抛断链。
		_, _ = s.clip.HandleFrame(f.Payload)
	case protocol.FrameFileAck:
		_ = s.fsender.HandleFrame(f.Header.FrameType, f.Payload)
	case protocol.FrameFileOffer, protocol.FrameFileChunk:
		if _, err := s.frecv.HandleFrame(f.Header.FrameType, f.Payload); err != nil {
			slog.Debug("link: file frame rejected", "err", err)
		}
	case protocol.FrameFileAbort:
		// ABORT 双向：发送侧（取消进行中传输）与接收侧（放弃收包缓冲）都消费。
		_ = s.fsender.HandleFrame(f.Header.FrameType, f.Payload)
		if _, err := s.frecv.HandleFrame(f.Header.FrameType, f.Payload); err != nil {
			slog.Debug("link: file abort on receiver", "err", err)
		}
	case protocol.FrameKeyDown, protocol.FrameKeyUp, protocol.FrameMouseMove,
		protocol.FrameMouseButton, protocol.FrameScroll:
		if h := s.eng.onPeerFrame; h != nil {
			h(s.peerName, f)
		}
	default:
		// 其余帧（§5.2 未接线类型）就地丢弃。
	}
	return true
}

func (s *session) handleIdentify(f *protocol.HIDFrame) bool {
	id, err := protocol.UnmarshalIdentify(f.Payload)
	if err != nil {
		// 版本不符等（§4.2：版本协商检查点）→ 0x01 后断链。
		s.respond(protocol.IdentifyVersionIncompat)
		s.setCloseReason("IDENTIFY 校验失败: " + err.Error())
		return false
	}
	if s.peerSeq == nil {
		s.peerSeq = protocol.NewSequenceTracker(id.SeqInit)
	}
	// 对端 IDENTIFY 自身也参与序列校验（首帧必须等于 seq_init）。
	if err := s.peerSeq.Check(f.Header.Sequence); err != nil {
		return true
	}

	s.mu.Lock()
	if s.peerName == "" {
		s.peerName = id.Name
	}
	s.peerPlatform = id.Platform
	s.mu.Unlock()

	status := s.eng.decidePeer(s)
	if status == protocol.IdentifyNeedConfirm {
		s.setState(StateAuthenticating)
		select {
		case st := <-s.confirmCh:
			status = st
		case <-time.After(pairConfirmTimeout):
			status = protocol.IdentifyRejected
		case <-s.done:
			return false
		}
		s.eng.resolvePending(s)
	}
	s.respond(status)
	if status == protocol.IdentifyAccepted {
		_ = s.eng.trust.Add(s.fingerprint, id.Name, s.peerCertDER)
		s.maybeOnline()
		return true
	}
	return false
}

func (s *session) handleIdentifyResponse(f *protocol.HIDFrame) bool {
	resp, err := protocol.UnmarshalIdentifyResponse(f.Payload)
	if err != nil {
		s.setCloseReason("IDENTIFY_RESPONSE 校验失败")
		return false
	}
	switch resp.Status {
	case protocol.IdentifyAccepted:
		s.peerAccepted = true
		s.mu.Lock()
		if s.peerName == "" {
			s.peerName = resp.Name
		}
		s.mu.Unlock()
		s.maybeOnline()
	case protocol.IdentifyNeedConfirm:
		// 对端在等它的用户确认；保持 authenticating，稍后会再来一帧最终结论。
		s.setState(StateAuthenticating)
	default: // 0x01 版本不符 / 0x02 拒绝
		s.setCloseReason(fmt.Sprintf("对端拒绝 (status 0x%02X)", resp.Status))
		return false
	}
	return true
}

// maybeOnline 双向都接受后进入在线态：清握手墙钟、起心跳/延迟计时器。
func (s *session) maybeOnline() {
	if !s.peerAccepted || !s.responded || s.responseStatus != protocol.IdentifyAccepted {
		return
	}
	s.mu.Lock()
	if s.state == StateOnline {
		s.mu.Unlock()
		return
	}
	s.state = StateOnline
	s.mu.Unlock()
	_ = s.conn.SetDeadline(time.Time{})
	s.eng.notify()

	go s.tickLoop()
}

// tickLoop 心跳发送 + 喂狗判定（§9.1）、延迟采样（§9.2）与文件 Pump 驱动。
func (s *session) tickLoop() {
	hbTick := time.NewTicker(protocol.HeartbeatInterval)
	defer hbTick.Stop()
	pingTick := time.NewTicker(protocol.LatencyPingInterval)
	defer pingTick.Stop()
	pumpTick := time.NewTicker(50 * time.Millisecond)
	defer pumpTick.Stop()
	for {
		select {
		case <-hbTick.C:
			slog.Debug("link: heartbeat sent")
			s.emit(protocol.FrameHeartbeat, nil)
			if s.hb.Tick() {
				return // onLost 已 teardown
			}
		case <-pingTick.C:
			t1 := uint64(time.Now().UnixMicro())
			s.lat.RecordPing(t1)
			payload := make([]byte, 8)
			binary.BigEndian.PutUint64(payload, t1)
			s.emit(protocol.FrameLatencyPing, payload)
		case <-pumpTick.C:
			if _, err := s.fsender.Pump(64); err != nil {
				slog.Debug("link: file pump error", "err", err)
			}
		case <-s.done:
			return
		}
	}
}

// ── 发送 ────────────────────────────────────────────────────────────────

func (s *session) sendIdentify() {
	payload := (&protocol.IdentifyPayload{
		Version:  protocol.ProtocolVersion,
		Platform: s.eng.cfg.Platform,
		Role:     protocol.RoleBoth,
		Caps:     s.eng.cfg.Caps,
		SeqInit:  uint32(s.mySeq.Current()),
		Name:     s.eng.cfg.Name,
	}).Marshal()
	s.emit(protocol.FrameIdentify, payload)
}

func (s *session) respond(status uint8) {
	s.responded = true
	s.responseStatus = status
	s.emit(protocol.FrameIdentifyRes, (&protocol.IdentifyResponsePayload{
		Status:   status,
		Platform: s.eng.cfg.Platform,
		Caps:     s.eng.cfg.Caps,
		Name:     s.eng.cfg.Name,
	}).Marshal())
	s.maybeOnline()
}

// sink 是 FrameStream 的出站接缝：非阻塞入队，满即断（慢对端不许拖死引擎）。
func (s *session) sink(f *protocol.HIDFrame) error {
	select {
	case s.send <- f:
		return nil
	default:
		s.teardown("send queue overflow")
		return errors.New("link: send queue overflow")
	}
}

func (s *session) emit(frameType uint8, payload []byte) {
	if err := s.stream.Emit(frameType, payload); err != nil {
		s.setCloseReason(err.Error())
		s.teardown("emit failed")
	}
}

func (s *session) writeLoop() {
	w := bufio.NewWriter(s.conn)
	for {
		select {
		case f := <-s.send:
			buf, err := protocol.AppendTCPFrame(nil, f, nil)
			if err != nil {
				continue // 单帧编不出来就丢帧，不拖垮连接
			}
			if _, err := w.Write(buf); err != nil {
				s.setCloseReason(err.Error())
				s.teardown("write failed")
				return
			}
			if err := w.Flush(); err != nil {
				s.setCloseReason(err.Error())
				s.teardown("write failed")
				return
			}
		case <-s.done:
			return
		}
	}
}

// ── 状态与收尾 ──────────────────────────────────────────────────────────

// Disconnect 用户主动断开：先发 DISCONNECT(0x00) 再关（§12.4）。
func (s *session) Disconnect() {
	s.mu.Lock()
	s.userClose = true
	s.mu.Unlock()
	s.emit(protocol.FrameDisconnect, []byte{0x00})
	s.teardown("user disconnect")
}

func (s *session) teardown(reason string) {
	s.closeOnce.Do(func() {
		s.mu.Lock()
		if s.closeReason == "" {
			s.closeReason = reason
		}
		s.mu.Unlock()
		close(s.done)
		_ = s.conn.Close()
	})
}

func (s *session) setState(st sessionState) {
	s.mu.Lock()
	if s.state != st {
		s.state = st
		s.mu.Unlock()
		s.eng.notify()
		return
	}
	s.mu.Unlock()
}

func (s *session) setCloseReason(reason string) {
	s.mu.Lock()
	if s.closeReason == "" {
		s.closeReason = reason
	}
	s.mu.Unlock()
}

func (s *session) snapshotLocked() LinkSnapshot {
	latMin, latMax, latAvg := s.lat.Stats()
	return LinkSnapshot{
		Name:        s.peerName,
		Instance:    InstanceOf(s.peerName),
		Addr:        s.remoteAddr.String(),
		Port:        s.remotePort,
		State:       string(s.state),
		LatencyMS:   s.latencyMS,
		LatMinMS:    latMin,
		LatMaxMS:    latMax,
		LatAvgMS:    latAvg,
		Samples:     s.sampleCount, // 环回 RTT 可为 0µs：有无样本要看计数而非值
		Platform:    s.peerPlatform.String(),
		Fingerprint: s.fingerprint,
		Trusted:     s.eng.trust.IsTrusted(s.fingerprint),
		Inbound:     s.inbound,
	}
}

// SendClipboard 经本会话发送剪贴板（§5.3 分段 + 接收侧重组）。
//
// 批量数据走阻塞入队（背压合法：剪贴板可以等 TCP 排空；输入帧仍走
// sink 的非阻塞路径，队列里有积压也只是短暂数十帧）。
func (s *session) SendClipboard(mime string, data []byte) error {
	chunks, err := protocol.SegmentClipboard(mime, data)
	if err != nil {
		return err
	}
	for i := range chunks {
		payload, err := chunks[i].Marshal()
		if err != nil {
			return err
		}
		f := &protocol.HIDFrame{
			Header: protocol.FrameHeader{
				FrameType: protocol.FrameClipboard,
				Sequence:  s.mySeq.Next(),
				Timestamp: nowUs(),
			},
			Payload: payload,
		}
		select {
		case s.send <- f:
		case <-s.done:
			return errors.New("link: session closed during clipboard send")
		}
	}
	return nil
}

// SendFile 经本会话发起文件传输（§7.4；≤16MiB 由协议校验）。
func (s *session) SendFile(name string, data []byte) error {
	_, err := s.fsender.OfferFile(name, data)
	return err
}

// InstanceOf 由设备名派生 mDNS 服务实例名（IDENTIFY 名 == TXT name）。
func InstanceOf(name string) string {
	if name == "" {
		return ""
	}
	return name + "._flowgrid._tcp.local."
}

func randSeqInit() uint32 {
	return rand.Uint32()
}

func nowUs() uint64 {
	return uint64(time.Now().UnixMicro())
}

func remotePort(addr net.Addr) int {
	if tcp, ok := addr.(*net.TCPAddr); ok {
		return tcp.Port
	}
	return 0
}
