// Command webdemo 是一个协议层 web 演示：用浏览器页面驱动 Go 协议库，演示四个
// 能力——设备发现（IntelligentLink 私有链：ECDSA 身份 + 组播签名通告 + 验签聚合
// + 指纹）、链接（identify 握手 + 链路规划）、剪切板（两 ClipboardSession 经真实
// 帧往返）、命令识别（键鼠 payload 编解码 + 跨平台键位重映射）。
//
// 设备发现走真实 UDP 组播（LanIDGroup:LanIDPort），但「发现协议」全在 protocol 包
// （lanid.go：签名/验签/聚合/信任链）；本文件只额外提供一个 ~30 行 UDP 字节泵
// （radio adapter）把字节搬进搬出，不含 HAL（输入注入）、GUI（AGENTS.md #28）。
package main

import (
	"crypto/ecdsa"
	"embed"
	"encoding/binary"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"io"
	"log"
	"net"
	"net/http"
	"os"
	"runtime"
	"strconv"
	"sync"
	"time"

	"github.com/flowgrid/protocol"
)

//go:embed index.html
var indexHTML embed.FS

// demoHMACKey 仅用于演示帧的 HMAC 完整性（§8.3），非真实会话密钥。
var demoHMACKey = []byte("flowgrid-webdemo-hmac-key-32b!")

// allowLocalPeers 决定「扫描周围设备」要不要把本机上的另一个 FlowGrid 实例
// 也算进来。默认不算：demo 与 DDE 客户端同机运行时，两边都往同一个组播端口
// 发通告，demo 会把「自己这台机器」列进周围设备——用户看到的就是扫到了自己。
// 置 1 可在同机联调（同机跑两个实例互相发现），代价正是上面这个问题。
var allowLocalPeers = os.Getenv("FLOWGRID_DEMO_ALLOW_LOCAL") == "1"

func main() {
	port := os.Getenv("PORT")
	if port == "" {
		port = "8080"
	}
	httpPort := 8080
	if p, err := strconv.Atoi(port); err == nil {
		httpPort = p
	}
	tcpPort := httpPort + 20000 // 连接通道 TCP 端口，从 HTTP 端口派生，避免同主机多实例冲突
	startDiscovery(tcpPort)     // IntelligentLink 私有链：ECDSA 身份 + UDP 组播 radio + TCP 连接通道
	http.HandleFunc("/", serveIndex)
	http.HandleFunc("/api/identity", handleIdentity)
	http.HandleFunc("/api/peers", handlePeers)
	http.HandleFunc("/api/connect", handleConnect)
	http.HandleFunc("/api/invites", handleInvites)
	http.HandleFunc("/api/respond", handleRespond)
	http.HandleFunc("/api/clipboard", handleClipboard)
	http.HandleFunc("/api/command", handleCommand)
	fmt.Printf("FlowGrid 协议层 web demo → http://localhost:%s（连接通道 TCP :%d，组播 %s:%d）\n",
		port, tcpPort, protocol.LanIDGroup, protocol.LanIDPort)
	if err := http.ListenAndServe(":"+port, nil); err != nil {
		fmt.Fprintln(os.Stderr, "server:", err)
		os.Exit(1)
	}
}

func serveIndex(w http.ResponseWriter, r *http.Request) {
	if r.URL.Path != "/" {
		http.NotFound(w, r)
		return
	}
	data, _ := indexHTML.ReadFile("index.html")
	w.Header().Set("Content-Type", "text/html; charset=utf-8")
	w.Write(data)
}

func writeJSON(w http.ResponseWriter, v any) {
	w.Header().Set("Content-Type", "application/json; charset=utf-8")
	_ = json.NewEncoder(w).Encode(v)
}

func fail(w http.ResponseWriter, err error) {
	writeJSON(w, map[string]any{"error": err.Error()})
}

// encodeFrame / decodeFrame 把 payload 打成带 HMAC 的线上字节再解回，模拟一次收发。
func encodeFrame(ft uint8, seq uint16, payload []byte) []byte {
	f := &protocol.HIDFrame{
		Header:  protocol.FrameHeader{FrameType: ft, Sequence: seq, Timestamp: 1_700_000_000_000},
		Payload: payload,
	}
	wire, err := f.EncodeBytesWithHMAC(demoHMACKey)
	if err != nil {
		return nil
	}
	return wire
}

// writeFrame / readFrame 是 demo 的 TCP 传输framing：4 字节大端长度前缀 + 线上字节。
func writeFrame(conn net.Conn, wire []byte) error {
	var hdr [4]byte
	binary.BigEndian.PutUint32(hdr[:], uint32(len(wire)))
	if _, err := conn.Write(hdr[:]); err != nil {
		return err
	}
	_, err := conn.Write(wire)
	return err
}

func readFrame(conn net.Conn) ([]byte, error) {
	var hdr [4]byte
	if _, err := io.ReadFull(conn, hdr[:]); err != nil {
		return nil, err
	}
	n := binary.BigEndian.Uint32(hdr[:])
	if n == 0 || n > 1<<20 {
		return nil, fmt.Errorf("bad frame length %d", n)
	}
	buf := make([]byte, n)
	if _, err := io.ReadFull(conn, buf); err != nil {
		return nil, err
	}
	return buf, nil
}

// identifyFrame / identifyResponseFrame 构造 §4.2 握手帧（带本节点身份）。
func (n *discNode) identifyFrame() []byte {
	id := &protocol.IdentifyPayload{
		Version:  protocol.ProtocolVersion,
		Platform: n.selfPlatform,
		Role:     protocol.RoleBoth,
		Caps:     protocol.CapIntelligentLink | protocol.CapDirectLink | protocol.CapDTLS13 | protocol.CapClipboard,
		SeqInit:  uint32(time.Now().Unix()),
		Name:     n.selfName,
	}
	_ = id.Validate()
	return encodeFrame(protocol.FrameIdentify, 1, id.Marshal())
}

func (n *discNode) identifyResponseFrame(status uint8) []byte {
	resp := &protocol.IdentifyResponsePayload{
		Status:   status,
		Platform: n.selfPlatform,
		Caps:     protocol.CapIntelligentLink | protocol.CapDirectLink | protocol.CapDTLS13 | protocol.CapClipboard,
		Name:     n.selfName,
	}
	return encodeFrame(protocol.FrameIdentifyRes, 2, resp.Marshal())
}

// ── ① 设备发现：IntelligentLink 私有链（lanid.go）+ 最小 UDP 组播 radio ─────

// invite 是一条收到的待接受连接请求（B 侧）：持有对方 TCP conn，接受/拒绝时
// 在这条 conn 上回 IDENTIFY_RESPONSE。
type invite struct {
	fromName string
	fromFp   string
	fromAddr string
	conn     net.Conn
	at       time.Time
}

// discNode 是本节点在私有链上的身份与发现状态。发现「协议」全在 protocol 包
// （lanid.go：ECDSA 签名/验签/聚合/信任链）；这里只持有 Announcer/Listener/
// TrustChain，并驱动 UDP 组播 + 一个 TCP 连接通道（demo 传输）。
type discNode struct {
	identity        *ecdsa.PrivateKey
	announcer       *protocol.LanIDAnnouncer
	listener        *protocol.LanIDListener
	trust           *protocol.TrustChain
	selfName        string
	selfPlatform    protocol.Platform
	selfFingerprint string
	tcpPort         int // demo TCP 连接通道端口（lanid 通告里 announce，对端据此 dial）

	mu        sync.Mutex
	connected map[string]bool    // 对端 fingerprint → 已建立连接
	connState map[string]string  // 对端 fingerprint → connecting/connected/rejected/failed
	invites   map[string]*invite // 收到的待接受连接邀请（key = 对端 name，含指纹短码故唯一）
	events    []string           // 发现/连接事件流水（供前端展示）

	// 本机接口地址集合（判「同机另一实例」用），带 TTL 缓存。
	ipMu    sync.Mutex
	ipCache map[string]bool
	ipAt    time.Time

	// 已记过「同机实例」的指纹：每 2s 一条通告，不去重的话事件流水会被刷爆。
	skippedLocal map[string]bool
}

var node *discNode

func platformFromGOOS() protocol.Platform {
	switch runtime.GOOS {
	case "darwin":
		return protocol.PlatformMacOS
	case "windows":
		return protocol.PlatformWindows
	default:
		return protocol.PlatformLinux
	}
}

// startDiscovery 生成本节点 ECDSA 身份，建 Announcer/Listener/TrustChain，起
// 后台 UDP 组播 radio（周期 Announce→sendto、recvfrom→Feed）+ TCP 连接通道监听。
func startDiscovery(tcpPort int) {
	ident, err := protocol.GenerateIdentityKeyPair()
	if err != nil {
		log.Printf("discovery: 生成身份失败: %v", err)
		return
	}
	host, _ := os.Hostname()
	fp := protocol.PublicKeyFingerprintFromBytes(protocol.IdentityPublicKeyBytes(ident))
	// 名字带指纹短码：同主机跑多个实例时可区分，且一眼看出哪个不是自己
	name := fmt.Sprintf("%s · %s", host, fp[:8])
	platform := platformFromGOOS()
	caps := protocol.CapIntelligentLink | protocol.CapDirectLink | protocol.CapDTLS13 |
		protocol.CapClipboard | protocol.CapFileTransfer
	// 通告里 announce 的是 TCP 连接通道端口（对端据此 dial 建连接），非组播端口
	ann, err := protocol.NewLanIDAnnouncer(ident, platform, caps, uint16(tcpPort), name)
	if err != nil {
		log.Printf("discovery: 建 Announcer 失败: %v", err)
		return
	}
	node = &discNode{
		identity:        ident,
		announcer:       ann,
		listener:        protocol.NewLanIDListener(),
		trust:           protocol.NewTrustChain(ident),
		selfName:        name,
		selfPlatform:    platform,
		selfFingerprint: fp,
		tcpPort:         tcpPort,
		connected:       map[string]bool{},
		connState:       map[string]string{},
		invites:         map[string]*invite{},
		skippedLocal:    map[string]bool{},
	}
	node.addEvent(fmt.Sprintf("本节点就绪：%s [%s] 指纹 %s（组播 %s:%d，连接通道 TCP :%d，每 %s 通告）",
		name, platform, node.selfFingerprint, protocol.LanIDGroup, protocol.LanIDPort, tcpPort, protocol.LanIDAnnouncePeriod))
	if allowLocalPeers {
		node.addEvent("同机实例计入周围设备（FLOWGRID_DEMO_ALLOW_LOCAL=1）")
	} else {
		node.addEvent("周围设备不含本机：同机另一实例的通告按源 IP 排除")
	}
	log.Printf("discovery: %s [%s] 指纹 %s @ %s:%d，TCP :%d", name, platform, node.selfFingerprint, protocol.LanIDGroup, protocol.LanIDPort, tcpPort)
	go radioLoop()
	go tcpListenLoop()
}

func (n *discNode) addEvent(s string) {
	n.mu.Lock()
	n.events = append(n.events, s)
	if len(n.events) > 50 {
		n.events = n.events[len(n.events)-50:]
	}
	n.mu.Unlock()
}

// localIPs 返回本机所有接口的单播地址（含回环）。
//
// 带 5s 缓存：DHCP 续租换了 IP 后判定要跟上，不能只在启动时算一次；而每次
// 通告都去遍历接口也没必要。
func (n *discNode) localIPs() map[string]bool {
	n.ipMu.Lock()
	defer n.ipMu.Unlock()
	if n.ipCache != nil && time.Since(n.ipAt) < 5*time.Second {
		return n.ipCache
	}
	ips := map[string]bool{"127.0.0.1": true, "::1": true}
	if ifaces, err := net.Interfaces(); err == nil {
		for _, ifi := range ifaces {
			addrs, err := ifi.Addrs()
			if err != nil {
				continue
			}
			for _, a := range addrs {
				if ipn, ok := a.(*net.IPNet); ok && ipn.IP != nil {
					ips[ipn.IP.String()] = true
				}
			}
		}
	}
	n.ipCache, n.ipAt = ips, time.Now()
	return ips
}

// isLocalPeer 判断对端是不是「本机上的另一个 FlowGrid 实例」。
//
// 判定依据是通告的来源 IP（LanIDPeer.Addr）落在本机接口地址里：组播是全网段
// 广播的，同机实例和真正在局域网另一台机器上的对端，在协议层看不出差别，
// 只有源 IP 能区分。
func (n *discNode) isLocalPeer(p protocol.LanIDPeer) bool {
	if p.Addr == nil {
		return false
	}
	return n.localIPs()[p.Addr.String()]
}

// noteLocalPeer 在事件流水里记一笔「同机实例已排除」。
//
// 要留痕：用户看到某台机器没进列表，应该知道是被同机规则排掉了，而不是凭空
// 消失。每台只记一次——通告每 2s 一条，不去重的话流水会被同一条刷爆。
func (n *discNode) noteLocalPeer(p protocol.LanIDPeer) {
	n.mu.Lock()
	if n.skippedLocal[p.Fingerprint] {
		n.mu.Unlock()
		return
	}
	n.skippedLocal[p.Fingerprint] = true
	n.mu.Unlock()
	n.addEvent(fmt.Sprintf("已排除同机实例 %s（源 IP %s 即本机地址，不属于「周围设备」；"+
		"同机联调设 FLOWGRID_DEMO_ALLOW_LOCAL=1 重启）", p.Name, p.Addr))
}

// radioLoop 是唯一的「radio adapter」：一个 UDP 组播 socket，把 lanid.go 的
// Announcer/Listener 接到空气里。协议逻辑（签名/验签/聚合）不在这里。
func radioLoop() {
	gaddr, err := net.ResolveUDPAddr("udp", fmt.Sprintf("%s:%d", protocol.LanIDGroup, protocol.LanIDPort))
	if err != nil {
		log.Printf("discovery radio: %v", err)
		return
	}
	// 发送：连接到组播组
	sendConn, err := net.DialUDP("udp", nil, gaddr)
	if err != nil {
		log.Printf("discovery radio: dial: %v", err)
		node.addEvent("UDP 组播发送失败：" + err.Error())
		return
	}
	defer sendConn.Close()
	// 接收：绑定端口并加入组播组（gaddr 必须含组 IP，否则 macOS 报 missing address）
	recvConn, err := net.ListenMulticastUDP("udp", nil, gaddr)
	if err != nil {
		log.Printf("discovery radio: 绑定组播失败: %v", err)
		node.addEvent("UDP 组播绑定失败：" + err.Error())
		return
	}
	defer recvConn.Close()

	// 周期通告（带 ECDSA 签名）
	go func() {
		t := time.NewTicker(protocol.LanIDAnnouncePeriod)
		defer t.Stop()
		for {
			if wire, err := node.announcer.Announce(); err == nil {
				_, _ = sendConn.Write(wire)
			}
			<-t.C
		}
	}()

	// 周期清理失联对端：超过 3 个通告周期没再见到的 peer 移除（死实例不留幽灵）
	go func() {
		t := time.NewTicker(protocol.LanIDAnnouncePeriod)
		defer t.Stop()
		for {
			<-t.C
			node.listener.Prune(3 * protocol.LanIDAnnouncePeriod)
		}
	}()

	// 接收 → Feed（验签 + 聚合）→ Poll 到新对端 → 握手
	buf := make([]byte, 4096)
	for {
		nread, from, err := recvConn.ReadFromUDP(buf)
		if err != nil {
			continue
		}
		node.listener.Feed(buf[:nread], from.IP)
		for _, p := range node.listener.Poll() {
			if p.Fingerprint == node.selfFingerprint {
				continue // 组播回环：收到自己的通告
			}
			if !allowLocalPeers && node.isLocalPeer(p) {
				node.noteLocalPeer(p)
				continue // 同机另一实例：不算「周围设备」
			}
			node.onPeerDiscovered(p)
		}
	}
}

// onPeerDiscovered：协议层验签通过、聚合成 LanIDPeer 后「告诉客户端」——记录事件、
// 并入信任链。真正的连接由用户点「连接」触发（见 connectTo），对端可接受/拒绝。
func (n *discNode) onPeerDiscovered(p protocol.LanIDPeer) {
	n.addEvent(fmt.Sprintf("发现对端 %s [%s] %s:%d 指纹 %s（验签通过，可发起连接）",
		p.Name, p.Platform, p.Addr, p.Port, p.Fingerprint))
	_ = n.trust.Append(p.PubKey, 1) // 信任链记录：本节点见过该对端
}

// ── ② 连接：真实 TCP 通道 + identify 握手，对端可接受/拒绝 ────────────────

// tcpListenLoop 监听连接通道端口；每条入站连接先收一条 IDENTIFY 帧，转成待接受
// 邀请（前端「接受/拒绝」）。TCP 传输归 demo，握手协议走 protocol 包。
func tcpListenLoop() {
	ln, err := net.Listen("tcp", fmt.Sprintf(":%d", node.tcpPort))
	if err != nil {
		log.Printf("tcp listen: %v", err)
		node.addEvent("TCP 连接通道监听失败：" + err.Error())
		return
	}
	node.addEvent(fmt.Sprintf("连接通道就绪：TCP :%d（等待对端连接）", node.tcpPort))
	for {
		conn, err := ln.Accept()
		if err != nil {
			continue
		}
		go handleInbound(conn)
	}
}

// handleInbound：B 侧收到 A 的连接——读 IDENTIFY 帧，登记为待接受邀请。
func handleInbound(conn net.Conn) {
	_ = conn.SetReadDeadline(time.Now().Add(10 * time.Second))
	wire, err := readFrame(conn)
	if err != nil {
		conn.Close()
		return
	}
	frame, _, err := protocol.DecodeFrameBytesWithHMAC(wire, demoHMACKey)
	if err != nil || frame.Header.FrameType != protocol.FrameIdentify {
		conn.Close()
		return
	}
	id, err := protocol.UnmarshalIdentify(frame.Payload)
	if err != nil {
		conn.Close()
		return
	}
	_ = conn.SetReadDeadline(time.Time{}) // 清读超时，等用户接受/拒绝
	inv := &invite{
		fromName: id.Name,
		fromFp:   node.matchPeerFingerprintByName(id.Name),
		fromAddr: conn.RemoteAddr().String(),
		conn:     conn,
		at:       time.Now(),
	}
	node.mu.Lock()
	node.invites[id.Name] = inv
	node.mu.Unlock()
	node.addEvent(fmt.Sprintf("收到来自 %s 的连接请求（IDENTIFY 校验通过），等待接受…", id.Name))
}

// matchPeerFingerprintByName 用对端 name（含指纹短码）在已发现列表里找完整指纹。
func (n *discNode) matchPeerFingerprintByName(name string) string {
	for _, p := range n.listener.Peers() {
		if p.Name == name {
			return p.Fingerprint
		}
	}
	return ""
}

// connectTo：A 侧发起连接——dial 对端 TCP 通道，发 IDENTIFY 帧，后台等 IDENTIFY_RESPONSE。
func (n *discNode) connectTo(fp string) error {
	var target *protocol.LanIDPeer
	peers := n.listener.Peers()
	for i := range peers {
		if peers[i].Fingerprint == fp {
			target = &peers[i]
			break
		}
	}
	if target == nil {
		return fmt.Errorf("未找到该对端（可能已失联）")
	}
	n.mu.Lock()
	if n.connState[fp] == "connecting" || n.connState[fp] == "connected" {
		n.mu.Unlock()
		return fmt.Errorf("已在连接中或已连接")
	}
	n.connState[fp] = "connecting"
	n.mu.Unlock()

	conn, err := net.DialTimeout("tcp", fmt.Sprintf("%s:%d", target.Addr, target.Port), 3*time.Second)
	if err != nil {
		n.mu.Lock()
		n.connState[fp] = "failed"
		n.mu.Unlock()
		return fmt.Errorf("dial %s:%d: %w", target.Addr, target.Port, err)
	}
	if err := writeFrame(conn, n.identifyFrame()); err != nil {
		conn.Close()
		n.mu.Lock()
		n.connState[fp] = "failed"
		n.mu.Unlock()
		return err
	}
	n.addEvent(fmt.Sprintf("→ 已向 %s 发起连接（TCP %s:%d），等待对端接受…", target.Name, target.Addr, target.Port))

	go func() {
		defer conn.Close()
		_ = conn.SetReadDeadline(time.Now().Add(30 * time.Second))
		wire, err := readFrame(conn)
		if err != nil {
			n.mu.Lock()
			n.connState[fp] = "failed"
			n.mu.Unlock()
			n.addEvent("✗ " + target.Name + " 未响应：" + err.Error())
			return
		}
		frame, _, err := protocol.DecodeFrameBytesWithHMAC(wire, demoHMACKey)
		if err != nil || frame.Header.FrameType != protocol.FrameIdentifyRes {
			n.mu.Lock()
			n.connState[fp] = "failed"
			n.mu.Unlock()
			n.addEvent("✗ " + target.Name + " 响应解析失败")
			return
		}
		resp, err := protocol.UnmarshalIdentifyResponse(frame.Payload)
		if err != nil {
			n.mu.Lock()
			n.connState[fp] = "failed"
			n.mu.Unlock()
			return
		}
		n.mu.Lock()
		if resp.Status == 0 {
			n.connState[fp] = "connected"
			n.connected[fp] = true
		} else {
			n.connState[fp] = "rejected"
		}
		n.mu.Unlock()
		if resp.Status == 0 {
			n.addEvent(fmt.Sprintf("✔ %s 已接受，连接建立（对端 %s）", target.Name, resp.Name))
		} else {
			n.addEvent(fmt.Sprintf("✗ %s 拒绝了连接请求", target.Name))
		}
	}()
	return nil
}

// respondInvite：B 侧接受/拒绝一条邀请——在持有的 conn 上回 IDENTIFY_RESPONSE。
func (n *discNode) respondInvite(name string, accept bool) error {
	n.mu.Lock()
	inv := n.invites[name]
	if inv != nil {
		delete(n.invites, name)
	}
	n.mu.Unlock()
	if inv == nil {
		return fmt.Errorf("邀请不存在或已处理")
	}
	status := uint8(0)
	if !accept {
		status = 0x01
	}
	err := writeFrame(inv.conn, n.identifyResponseFrame(status))
	inv.conn.Close()
	if accept {
		if err == nil && inv.fromFp != "" {
			n.mu.Lock()
			n.connected[inv.fromFp] = true
			n.connState[inv.fromFp] = "connected"
			n.mu.Unlock()
		}
		n.addEvent(fmt.Sprintf("✔ 已接受 %s 的连接", name))
	} else {
		n.addEvent(fmt.Sprintf("✗ 已拒绝 %s 的连接", name))
	}
	return err
}

// GET /api/identity — 本节点身份（名称/平台/指纹/组播地址/信任链长度/事件流水）
func handleIdentity(w http.ResponseWriter, r *http.Request) {
	if node == nil {
		fail(w, fmt.Errorf("发现未启动"))
		return
	}
	node.mu.Lock()
	events := append([]string(nil), node.events...)
	node.mu.Unlock()
	writeJSON(w, map[string]any{
		"name":        node.selfName,
		"platform":    node.selfPlatform.String(),
		"fingerprint": node.selfFingerprint,
		"group":       protocol.LanIDGroup,
		"port":        protocol.LanIDPort,
		"trustCount":  len(node.trust.Records()),
		"allowLocal":  allowLocalPeers,
		"events":      events,
	})
}

// GET /api/peers — 实时发现的局域网同协议对端（验签通过 + 指纹 + 握手状态）
func handlePeers(w http.ResponseWriter, r *http.Request) {
	if node == nil {
		fail(w, fmt.Errorf("发现未启动"))
		return
	}
	node.mu.Lock()
	events := append([]string(nil), node.events...)
	// 复制一份再解锁：直接持有 map 引用的话，后台 connectTo/onPeerDiscovered 写 map 与
	// 这里读 map 并发，Go 的 map 并发读写会直接 panic。
	connected := make(map[string]bool, len(node.connected))
	for k, v := range node.connected {
		connected[k] = v
	}
	connState := make(map[string]string, len(node.connState))
	for k, v := range node.connState {
		connState[k] = v
	}
	node.mu.Unlock()

	out := []map[string]any{}
	filteredLocal := 0
	for _, p := range node.listener.Peers() {
		if p.Fingerprint == node.selfFingerprint {
			continue
		}
		if !allowLocalPeers && node.isLocalPeer(p) {
			filteredLocal++
			continue
		}
		out = append(out, map[string]any{
			"name":        p.Name,
			"platform":    p.Platform.String(),
			"fingerprint": p.Fingerprint,
			"addr":        p.Addr.String(),
			"port":        p.Port,
			"caps":        fmt.Sprintf("0x%02x", uint8(p.Caps)),
			"firstSeen":   p.FirstSeen.Format("15:04:05"),
			"handshake":   connected[p.Fingerprint],
			"connState": func() string {
				if connected[p.Fingerprint] {
					return "connected"
				}
				return connState[p.Fingerprint] // ""/connecting/rejected/failed
			}(),
		})
	}
	writeJSON(w, map[string]any{"peers": out, "events": events, "filteredLocal": filteredLocal})
}

// POST /api/connect?fp=<fingerprint> — 发起连接到某对端（A 侧）
func handleConnect(w http.ResponseWriter, r *http.Request) {
	if node == nil {
		fail(w, fmt.Errorf("发现未启动"))
		return
	}
	if err := node.connectTo(r.URL.Query().Get("fp")); err != nil {
		fail(w, err)
		return
	}
	writeJSON(w, map[string]any{"ok": true})
}

// GET /api/invites — 收到的待接受连接邀请（B 侧）
func handleInvites(w http.ResponseWriter, r *http.Request) {
	if node == nil {
		fail(w, fmt.Errorf("发现未启动"))
		return
	}
	node.mu.Lock()
	out := []map[string]any{}
	for name, inv := range node.invites {
		out = append(out, map[string]any{"name": name, "addr": inv.fromAddr, "at": inv.at.Format("15:04:05")})
	}
	node.mu.Unlock()
	writeJSON(w, map[string]any{"invites": out})
}

// POST /api/respond?name=<name>&accept=1|0 — 接受/拒绝邀请（B 侧）
func handleRespond(w http.ResponseWriter, r *http.Request) {
	if node == nil {
		fail(w, fmt.Errorf("发现未启动"))
		return
	}
	name := r.URL.Query().Get("name")
	accept := r.URL.Query().Get("accept") == "1"
	if err := node.respondInvite(name, accept); err != nil {
		fail(w, err)
		return
	}
	writeJSON(w, map[string]any{"ok": true})
}

// ── ② 剪切板：两个 ClipboardSession 经真实帧往返（§5.3）─────────────────

func handleClipboard(w http.ResponseWriter, r *http.Request) {
	text := r.URL.Query().Get("text")
	if text == "" {
		text = "Hello from FlowGrid 协议层 🚀"
	}

	var received *protocol.Clipboard
	frameCount := 0
	wireSamples := []string{}

	sessionB := protocol.NewClipboardSession(func(ft uint8, payload []byte) error { return nil })
	// A 的发送接缝 = FrameStream；每发出一帧就上链（编码→解码）喂给 B。
	stream := protocol.NewFrameStream(func(f *protocol.HIDFrame) error {
		wire, err := f.EncodeBytesWithHMAC(demoHMACKey)
		if err != nil {
			return err
		}
		if len(wireSamples) < 2 {
			wireSamples = append(wireSamples, hex.EncodeToString(wire))
		}
		decoded, _, err := protocol.DecodeFrameBytesWithHMAC(wire, demoHMACKey)
		if err != nil {
			return err
		}
		frameCount++
		if c, err := sessionB.HandleFrame(decoded.Payload); err == nil && c != nil {
			received = c
		}
		return nil
	}, protocol.NewSequenceTracker(0))

	sessionA := protocol.NewClipboardSession(stream.Emit)
	if err := sessionA.SendClipboard("text/plain", []byte(text)); err != nil {
		fail(w, err)
		return
	}

	resp := map[string]any{"sent": text, "frameCount": frameCount, "wireSamples": wireSamples}
	if received != nil {
		resp["received"] = map[string]any{"mime": received.MIMEType, "data": string(received.Data)}
		resp["match"] = string(received.Data) == text
	} else {
		resp["error"] = "对端未重组出剪贴板"
	}
	writeJSON(w, resp)
}

// ── ③ 命令识别：键鼠 payload 编解码往返 + 跨平台键位重映射 ────────────────

func handleCommand(w http.ResponseWriter, r *http.Request) {
	cmd := r.URL.Query().Get("cmd")
	var ft uint8
	var payload []byte
	var desc string
	switch cmd {
	case "cmd+c":
		p := &protocol.KeyDownPayload{KeyCode: protocol.HIDKeyC, Modifiers: protocol.ModLeftMeta}
		ft, payload, desc = protocol.FrameKeyDown, p.Marshal(), "按下 Cmd+C（Mac 复制）"
	case "move":
		p := &protocol.MouseMovePayload{DeltaX: 120, DeltaY: -45}
		ft, payload, desc = protocol.FrameMouseMove, p.Marshal(), "鼠标移动 ΔX=+120 ΔY=-45"
	case "click":
		p := &protocol.MouseButtonPayload{ButtonID: protocol.MouseButtonLeft, State: 0x01}
		ft, payload, desc = protocol.FrameMouseButton, p.Marshal(), "鼠标左键按下"
	case "scroll":
		p := &protocol.ScrollPayload{DeltaY: 3, DeltaX: 0}
		ft, payload, desc = protocol.FrameScroll, p.Marshal(), "滚轮向上 3 格"
	default:
		cmd = "cmd+c"
		p := &protocol.KeyDownPayload{KeyCode: protocol.HIDKeyC, Modifiers: protocol.ModLeftMeta}
		ft, payload, desc = protocol.FrameKeyDown, p.Marshal(), "按下 Cmd+C（Mac 复制）"
	}

	wire := encodeFrame(ft, 1, payload)
	decoded, _, err := protocol.DecodeFrameBytesWithHMAC(wire, demoHMACKey)
	if err != nil {
		fail(w, err)
		return
	}

	recognized := map[string]any{}
	switch ft {
	case protocol.FrameKeyDown:
		kp, _ := protocol.UnmarshalKeyDown(decoded.Payload)
		recognized = map[string]any{"事件": "keydown", "键码": fmt.Sprintf("0x%04x", kp.KeyCode), "修饰": fmt.Sprintf("0x%02x", kp.Modifiers)}
	case protocol.FrameMouseMove:
		mp, _ := protocol.UnmarshalMouseMove(decoded.Payload)
		recognized = map[string]any{"事件": "mousemove", "ΔX": mp.DeltaX, "ΔY": mp.DeltaY}
	case protocol.FrameMouseButton:
		bp, _ := protocol.UnmarshalMouseButton(decoded.Payload)
		state := "释放"
		if bp.State == 0x01 {
			state = "按下"
		}
		recognized = map[string]any{"事件": "mousebutton", "按键": bp.ButtonID, "状态": state}
	case protocol.FrameScroll:
		sp, _ := protocol.UnmarshalScroll(decoded.Payload)
		recognized = map[string]any{"事件": "scroll", "ΔY": sp.DeltaY, "ΔX": sp.DeltaX}
	}

	// 跨平台重映射：Mac 的 Cmd(Meta) 到 Linux 落成 Ctrl
	remap := map[string]any{
		"修饰掩码": fmt.Sprintf("Mac 0x%02x(Meta) → Linux 0x%02x(Ctrl)", protocol.ModLeftMeta, protocol.RemapModifiers(protocol.ModLeftMeta, protocol.PlatformMacOS, protocol.PlatformLinux)),
		"修饰键码": fmt.Sprintf("Mac 0x%04x(LMeta) → Linux 0x%04x(LCtrl)", protocol.HIDKeyLMeta, protocol.RemapModifierKey(protocol.HIDKeyLMeta, protocol.PlatformMacOS, protocol.PlatformLinux)),
	}

	writeJSON(w, map[string]any{
		"command":    cmd,
		"desc":       desc,
		"wireHex":    hex.EncodeToString(wire),
		"wireLen":    len(wire),
		"recognized": recognized,
		"remap":      remap,
	})
}
