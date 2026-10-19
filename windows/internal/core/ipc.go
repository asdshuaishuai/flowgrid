// IPC 层：命名管道上的 JSON Lines 信封协议（openspec/changes/windows-client/
// specs/windows-ipc）。请求带 id，响应原样带回 id；device.changed 等事件无 id
// 单向推送。传输层用 Go stdlib 的 "pipe" 网络（Windows 命名管道），零依赖。
package core

import (
	"bufio"
	"encoding/json"
	"net"
	"strings"
	"sync"

	"github.com/Microsoft/go-winio"
	"github.com/flowgrid/protocol"

	"github.com/flowgrid/windows/internal/hub"
	"github.com/flowgrid/windows/internal/link"
)

const envelopeVersion = 1

type envelope struct {
	V       int             `json:"v"`
	Type    string          `json:"type"`
	ID      string          `json:"id,omitempty"`
	Payload json.RawMessage `json:"payload,omitempty"`
}

// peerDTO 是 DiscoveredPeer 的 IPC 投影。net.IP 会被 encoding/json 编成
// base64，地址必须在这里显式转字符串，别把 IP 直接喂给 json。
type peerDTO struct {
	Name       string `json:"name"`
	Instance   string `json:"instance"`
	Addr       string `json:"addr"`
	Port       uint16 `json:"port"`
	Platform   string `json:"platform"`
	Version    string `json:"version"`
	Caps       string `json:"caps"`
	Encryption string `json:"encryption"`
}

func toPeerDTO(p protocol.DiscoveredPeer) peerDTO {
	return peerDTO{
		Name:       p.Name,
		Instance:   p.Instance,
		Addr:       p.Addr.String(),
		Port:       p.Port,
		Platform:   p.Platform,
		Version:    p.Version,
		Caps:       p.Caps,
		Encryption: p.Encryption,
	}
}

func toPeerDTOs(peers []protocol.DiscoveredPeer) []peerDTO {
	out := make([]peerDTO, 0, len(peers))
	for _, p := range peers {
		out = append(out, toPeerDTO(p))
	}
	return out
}

func mustJSON(v any) json.RawMessage {
	b, err := json.Marshal(v)
	if err != nil {
		return json.RawMessage(`{}`)
	}
	return b
}

// ListenPipe 创建 IPC 管道监听。缓冲必须显式给：winio 零值配额（nil 配置）下
// 对端没有 pending read 时写入会永久阻塞（本机实测复现），64KB 让应答与广播
// 突发先落内核缓冲，不依赖对端恰好正在读。
func ListenPipe(name string) (net.Listener, error) {
	return winio.ListenPipe(name, &winio.PipeConfig{
		InputBufferSize:  1 << 16,
		OutputBufferSize: 1 << 16,
	})
}

// LinkEngine 是连接引擎对 IPC 层暴露的面（internal/link.Engine 满足）。
// 用接口而不是直接依赖，保持发现/连接两层与 IPC 的松耦合。
type LinkEngine interface {
	SelfInfo() link.SelfInfo
	Snapshots() []link.LinkSnapshot
	PendingPairs() []link.PairPending
	Connect(instance string) error
	Disconnect(instance string) bool
	ResolvePair(id string, accept bool) bool
	OnChange(fn func()) (cancel func())
}

// InputHub 是输入/剪贴板/文件中枢对 IPC 层暴露的面。
type InputHub interface {
	Status() hub.InputStatus
	SwitchTo(index int) error
	SetClipboardText(text string) error
	ClipboardText() (string, bool)
	SendFile(instance, path string) error
	Transfers() []hub.TransferInfo
	KeymapStatus() hub.KeymapStatus
	OnChange(fn func()) (cancel func())
	ClipboardHistory() []hub.ClipboardEntryMeta
	ClipboardEntryText(index int) (string, bool)
	WriteClipboardEntry(index int) error
	ClearClipboardHistory()
	SetClipboardCap(n int)
	SetClipSync(enabled bool)
	ClipSyncEnabled() bool
	ClipboardCount() int
	SetAutoAccept(enabled bool)
	AutoAccept() bool
	TrustEntries() []link.TrustView
	TrustRevoke(fingerprint string) error
}

// Server 在已监听的 pipe listener 上接受 UI 连接并分发消息。
type Server struct {
	core   *Core
	engine LinkEngine
	hub    InputHub
	ln     net.Listener

	mu     sync.Mutex
	closed bool
	conns  map[*pipeConn]struct{}

	cancelWatch func()
	wg          sync.WaitGroup
}

func Serve(ln net.Listener, c *Core, engine LinkEngine, input InputHub) *Server {
	s := &Server{core: c, engine: engine, hub: input, ln: ln, conns: map[*pipeConn]struct{}{}}
	s.cancelWatch = c.OnChange(func() {
		s.broadcast("device.changed", map[string]any{"peers": toPeerDTOs(c.Peers())})
	})
	if engine != nil {
		engine.OnChange(s.BroadcastLinkState)
	}
	if input != nil {
		input.OnChange(s.BroadcastInputState)
	}
	s.wg.Add(1)
	go s.acceptLoop()
	return s
}

// BroadcastLinkState 推送连接快照与待确认配对（全量语义，同 devices）。
func (s *Server) BroadcastLinkState() {
	if s.engine == nil {
		return
	}
	s.broadcast("peers.changed", map[string]any{
		"links":   s.engine.Snapshots(),
		"pending": s.engine.PendingPairs(),
	})
	s.broadcast("pair.pending", map[string]any{
		"pending": s.engine.PendingPairs(),
	})
}

// BroadcastInputState 推送托管状态与文件传输进度。
func (s *Server) BroadcastInputState() {
	if s.hub == nil {
		return
	}
	s.broadcast("input.changed", s.hub.Status())
	s.broadcast("file.progress", map[string]any{"transfers": s.hub.Transfers()})
}

func (s *Server) acceptLoop() {
	defer s.wg.Done()
	for {
		nc, err := s.ln.Accept()
		if err != nil {
			return
		}
		s.wg.Add(1)
		go func() {
			defer s.wg.Done()
			s.handleConn(nc)
		}()
	}
}

// pipeConn 是一条 UI 连接的出站通道：应答与广播全部经 post 排队，由唯一的
// writeLoop 落盘。队列满 = 客户端跟不上 = 断开——广播跑在核心循环上，
// 慢 UI 的阻塞写绝不允许把发现与清扫拖死（为故障设计）。
type pipeConn struct {
	nc   net.Conn
	enc  *json.Encoder
	send chan envelope
}

const sendQueueDepth = 64

func newPipeConn(nc net.Conn) *pipeConn {
	pc := &pipeConn{nc: nc, enc: json.NewEncoder(nc), send: make(chan envelope, sendQueueDepth)}
	go pc.writeLoop()
	return pc
}

func (p *pipeConn) writeLoop() {
	for env := range p.send {
		if p.enc.Encode(env) != nil {
			return // 连接已坏，handleConn 的 Scan 也会随之退出
		}
	}
}

// post 入队一帧。send 永不 close（Stop 只关 nc，writeLoop 因写错误退出），
// 所以不存在 send-on-closed；溢出的连接被直接断开，重连是 UI 的职责。
func (p *pipeConn) post(env envelope) {
	select {
	case p.send <- env:
	default:
		p.nc.Close()
	}
}

func (p *pipeConn) reply(typ, id string, payload any) {
	p.post(envelope{V: envelopeVersion, Type: typ, ID: id, Payload: mustJSON(payload)})
}

func (s *Server) handleConn(nc net.Conn) {
	pc := newPipeConn(nc)
	s.mu.Lock()
	if s.closed {
		s.mu.Unlock()
		nc.Close()
		return
	}
	s.conns[pc] = struct{}{}
	s.mu.Unlock()
	defer func() {
		s.mu.Lock()
		delete(s.conns, pc)
		s.mu.Unlock()
		nc.Close()
	}()

	sc := bufio.NewScanner(nc)
	sc.Buffer(make([]byte, 4096), 1<<20)
	for sc.Scan() {
		line := strings.TrimSpace(sc.Text())
		if line == "" {
			continue
		}
		var env envelope
		if err := json.Unmarshal([]byte(line), &env); err != nil {
			pc.reply("error", "", map[string]string{"message": "bad envelope: " + err.Error()})
			continue
		}
		s.dispatch(pc, &env)
	}
}

func (s *Server) dispatch(pc *pipeConn, env *envelope) {
	if env.V != envelopeVersion {
		pc.reply("error", env.ID, map[string]any{"message": "unsupported envelope version", "got": env.V})
		return
	}
	switch env.Type {
	case "hello":
		pc.reply("hello.ok", env.ID, map[string]string{"name": "fgcore", "version": Version})
	case "status.get":
		pc.reply("status", env.ID, s.core.Status())
	case "devices.list":
		pc.reply("devices", env.ID, map[string]any{"peers": toPeerDTOs(s.core.Peers())})
	case "devices.add":
		var req struct {
			Name string `json:"name"`
			Addr string `json:"addr"`
			Port uint16 `json:"port"`
		}
		if err := json.Unmarshal(env.Payload, &req); err != nil || req.Name == "" || req.Addr == "" || req.Port == 0 {
			pc.replyError(env.ID, "devices.add 需要 name/addr/port 字段")
			return
		}
		s.core.AddManualPeer(req.Name, req.Addr, req.Port)
		pc.reply("devices.ok", env.ID, map[string]string{"name": req.Name})
	case "input.status":
		if s.hub == nil {
			pc.replyError(env.ID, "input hub not running")
			return
		}
		pc.reply("input.status.ok", env.ID, s.hub.Status())
	case "switch.request":
		if s.hub == nil {
			pc.replyError(env.ID, "input hub not running")
			return
		}
		var req struct {
			Index int `json:"index"`
		}
		if err := json.Unmarshal(env.Payload, &req); err != nil {
			pc.replyError(env.ID, "switch.request 需要 index 字段")
			return
		}
		if err := s.hub.SwitchTo(req.Index); err != nil {
			pc.replyError(env.ID, err.Error())
			return
		}
		pc.reply("switch.ok", env.ID, req)
	case "clipboard.set":
		if s.hub == nil {
			pc.replyError(env.ID, "input hub not running")
			return
		}
		var req struct {
			Text string `json:"text"`
		}
		if err := json.Unmarshal(env.Payload, &req); err != nil {
			pc.replyError(env.ID, "clipboard.set 需要 text 字段")
			return
		}
		if err := s.hub.SetClipboardText(req.Text); err != nil {
			pc.replyError(env.ID, err.Error())
			return
		}
		pc.reply("clipboard.ok", env.ID, nil)
	case "clipboard.text":
		if s.hub == nil {
			pc.replyError(env.ID, "input hub not running")
			return
		}
		text, ok := s.hub.ClipboardText()
		pc.reply("clipboard.text.ok", env.ID, map[string]any{"text": text, "ok": ok})
	case "file.send":
		if s.hub == nil {
			pc.replyError(env.ID, "input hub not running")
			return
		}
		var req struct {
			Instance string `json:"instance"`
			Path     string `json:"path"`
		}
		if err := json.Unmarshal(env.Payload, &req); err != nil || req.Instance == "" || req.Path == "" {
			pc.replyError(env.ID, "file.send 需要 instance/path 字段")
			return
		}
		if err := s.hub.SendFile(req.Instance, req.Path); err != nil {
			pc.replyError(env.ID, err.Error())
			return
		}
		pc.reply("file.ok", env.ID, map[string]string{"instance": req.Instance})
	case "file.transfers":
		if s.hub == nil {
			pc.replyError(env.ID, "input hub not running")
			return
		}
		pc.reply("file.transfers.ok", env.ID, map[string]any{"transfers": s.hub.Transfers()})
	case "keymap.status":
		if s.hub == nil {
			pc.replyError(env.ID, "input hub not running")
			return
		}
		pc.reply("keymap.status.ok", env.ID, s.hub.KeymapStatus())
	case "clipboard.history":
		if s.hub == nil {
			pc.replyError(env.ID, "input hub not running")
			return
		}
		pc.reply("clipboard.history.ok", env.ID, map[string]any{"entries": s.hub.ClipboardHistory()})
	case "clipboard.entry":
		if s.hub == nil {
			pc.replyError(env.ID, "input hub not running")
			return
		}
		var req struct {
			Index int `json:"index"`
		}
		if err := json.Unmarshal(env.Payload, &req); err != nil {
			pc.replyError(env.ID, "clipboard.entry 需要 index 字段")
			return
		}
		text, ok := s.hub.ClipboardEntryText(req.Index)
		if !ok {
			pc.replyError(env.ID, "no such entry")
			return
		}
		pc.reply("clipboard.entry.ok", env.ID, map[string]any{"index": req.Index, "text": text})
	case "clipboard.write":
		if s.hub == nil {
			pc.replyError(env.ID, "input hub not running")
			return
		}
		var req struct {
			Index int `json:"index"`
		}
		if err := json.Unmarshal(env.Payload, &req); err != nil {
			pc.replyError(env.ID, "clipboard.write 需要 index 字段")
			return
		}
		if err := s.hub.WriteClipboardEntry(req.Index); err != nil {
			pc.replyError(env.ID, err.Error())
			return
		}
		pc.reply("clipboard.write.ok", env.ID, map[string]int{"index": req.Index})
	case "clipboard.clear":
		if s.hub == nil {
			pc.replyError(env.ID, "input hub not running")
			return
		}
		s.hub.ClearClipboardHistory()
		pc.reply("clipboard.clear.ok", env.ID, nil)
	case "clipboard.cap":
		if s.hub == nil {
			pc.replyError(env.ID, "input hub not running")
			return
		}
		var req struct {
			Cap int `json:"cap"`
		}
		if err := json.Unmarshal(env.Payload, &req); err != nil || req.Cap <= 0 {
			pc.replyError(env.ID, "clipboard.cap 需要 cap 字段")
			return
		}
		s.hub.SetClipboardCap(req.Cap)
		pc.reply("clipboard.cap.ok", env.ID, map[string]int{"cap": req.Cap})
	case "clipboard.enable":
		if s.hub == nil {
			pc.replyError(env.ID, "input hub not running")
			return
		}
		var req struct {
			Enabled bool `json:"enabled"`
		}
		if err := json.Unmarshal(env.Payload, &req); err != nil {
			pc.replyError(env.ID, "clipboard.enable 需要 enabled 字段")
			return
		}
		s.hub.SetClipSync(req.Enabled)
		pc.reply("clipboard.enable.ok", env.ID, map[string]bool{"enabled": req.Enabled})
	case "config.autoaccept":
		if s.hub == nil {
			pc.replyError(env.ID, "input hub not running")
			return
		}
		var req struct {
			Enabled bool `json:"enabled"`
		}
		if err := json.Unmarshal(env.Payload, &req); err != nil {
			pc.replyError(env.ID, "config.autoaccept 需要 enabled 字段")
			return
		}
		s.hub.SetAutoAccept(req.Enabled)
		pc.reply("config.autoaccept.ok", env.ID, map[string]bool{"enabled": req.Enabled})
	case "trust.list":
		if s.hub == nil {
			pc.replyError(env.ID, "input hub not running")
			return
		}
		pc.reply("trust.list.ok", env.ID, map[string]any{"entries": s.hub.TrustEntries()})
	case "trust.revoke":
		if s.hub == nil {
			pc.replyError(env.ID, "input hub not running")
			return
		}
		var req struct {
			Fingerprint string `json:"fingerprint"`
		}
		if err := json.Unmarshal(env.Payload, &req); err != nil || req.Fingerprint == "" {
			pc.replyError(env.ID, "trust.revoke 需要 fingerprint 字段")
			return
		}
		if err := s.hub.TrustRevoke(req.Fingerprint); err != nil {
			pc.replyError(env.ID, err.Error())
			return
		}
		pc.reply("trust.revoke.ok", env.ID, map[string]string{"fingerprint": req.Fingerprint})
	case "self.info":
		if s.engine == nil {
			pc.replyError(env.ID, "link engine not running")
			return
		}
		pc.reply("self", env.ID, s.engine.SelfInfo())
	case "peers.list":
		if s.engine == nil {
			pc.replyError(env.ID, "link engine not running")
			return
		}
		pc.reply("peers", env.ID, map[string]any{
			"links":   s.engine.Snapshots(),
			"pending": s.engine.PendingPairs(),
		})
	case "connect.request":
		if s.engine == nil {
			pc.replyError(env.ID, "link engine not running")
			return
		}
		var req struct {
			Instance string `json:"instance"`
		}
		if err := json.Unmarshal(env.Payload, &req); err != nil || req.Instance == "" {
			pc.replyError(env.ID, "connect.request 需要 instance 字段")
			return
		}
		if err := s.engine.Connect(req.Instance); err != nil {
			pc.replyError(env.ID, err.Error())
			return
		}
		pc.reply("connect.ok", env.ID, map[string]string{"instance": req.Instance})
	case "disconnect.request":
		if s.engine == nil {
			pc.replyError(env.ID, "link engine not running")
			return
		}
		var req struct {
			Instance string `json:"instance"`
		}
		if err := json.Unmarshal(env.Payload, &req); err != nil || req.Instance == "" {
			pc.replyError(env.ID, "disconnect.request 需要 instance 字段")
			return
		}
		if !s.engine.Disconnect(req.Instance) {
			pc.replyError(env.ID, "no such link: "+req.Instance)
			return
		}
		pc.reply("disconnect.ok", env.ID, map[string]string{"instance": req.Instance})
	case "pair.accept", "pair.reject":
		if s.engine == nil {
			pc.replyError(env.ID, "link engine not running")
			return
		}
		var req struct {
			ID string `json:"id"`
		}
		if err := json.Unmarshal(env.Payload, &req); err != nil || req.ID == "" {
			pc.replyError(env.ID, "pair 消息需要 id 字段")
			return
		}
		ok := s.engine.ResolvePair(req.ID, env.Type == "pair.accept")
		pc.reply("pair.ok", env.ID, map[string]any{"id": req.ID, "resolved": ok})
	default:
		pc.replyError(env.ID, "unknown type: "+env.Type)
	}
}

func (pc *pipeConn) replyError(id, msg string) {
	pc.reply("error", id, map[string]string{"message": msg})
}

func (s *Server) broadcast(typ string, payload any) {
	s.mu.Lock()
	if s.closed {
		s.mu.Unlock()
		return
	}
	conns := make([]*pipeConn, 0, len(s.conns))
	for pc := range s.conns {
		conns = append(conns, pc)
	}
	s.mu.Unlock()
	env := envelope{V: envelopeVersion, Type: typ, Payload: mustJSON(payload)}
	for _, pc := range conns {
		pc.post(env)
	}
}

// Stop 停止接受新连接、断开全部 UI 连接并等待 goroutine 退出。
func (s *Server) Stop() {
	s.mu.Lock()
	if s.closed {
		s.mu.Unlock()
		return
	}
	s.closed = true
	conns := make([]*pipeConn, 0, len(s.conns))
	for pc := range s.conns {
		conns = append(conns, pc)
	}
	s.conns = map[*pipeConn]struct{}{}
	s.mu.Unlock()

	s.cancelWatch()
	for _, pc := range conns {
		pc.nc.Close()
	}
	s.ln.Close()
	s.wg.Wait()
}
