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
	"encoding/hex"
	"encoding/json"
	"fmt"
	"log"
	"net"
	"net/http"
	"os"
	"runtime"
	"sync"
	"time"

	"github.com/flowgrid/protocol"
)

//go:embed index.html
var indexHTML embed.FS

// demoHMACKey 仅用于演示帧的 HMAC 完整性（§8.3），非真实会话密钥。
var demoHMACKey = []byte("flowgrid-webdemo-hmac-key-32b!")

func main() {
	port := os.Getenv("PORT")
	if port == "" {
		port = "8080"
	}
	startDiscovery() // IntelligentLink 私有链发现：ECDSA 身份 + UDP 组播 radio
	http.HandleFunc("/", serveIndex)
	http.HandleFunc("/api/identity", handleIdentity)
	http.HandleFunc("/api/peers", handlePeers)
	http.HandleFunc("/api/link", handleLink)
	http.HandleFunc("/api/clipboard", handleClipboard)
	http.HandleFunc("/api/command", handleCommand)
	fmt.Printf("FlowGrid 协议层 web demo → http://localhost:%s\n", port)
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

// ── ① 设备发现：IntelligentLink 私有链（lanid.go）+ 最小 UDP 组播 radio ─────

// discNode 是本节点在私有链上的身份与发现状态。发现「协议」全在 protocol 包
// （lanid.go：ECDSA 签名/验签/聚合/信任链）；这里只持有 Announcer/Listener/
// TrustChain，并驱动一个 UDP 字节泵把字节搬进搬出。
type discNode struct {
	identity        *ecdsa.PrivateKey
	announcer       *protocol.LanIDAnnouncer
	listener        *protocol.LanIDListener
	trust           *protocol.TrustChain
	selfName        string
	selfPlatform    protocol.Platform
	selfFingerprint string

	mu        sync.Mutex
	connected map[string]bool // 对端 fingerprint → 已发起 identify 握手
	events    []string        // 发现/握手事件流水（供前端展示）
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
// 后台 UDP 组播 radio：周期 Announce→sendto、recvfrom→Feed（验签+聚合）。
func startDiscovery() {
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
	ann, err := protocol.NewLanIDAnnouncer(ident, platform, caps, protocol.LanIDPort, name)
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
		connected:       map[string]bool{},
	}
	node.addEvent(fmt.Sprintf("本节点就绪：%s [%s] 指纹 %s（组播 %s:%d，每 %s 通告）",
		name, platform, node.selfFingerprint, protocol.LanIDGroup, protocol.LanIDPort, protocol.LanIDAnnouncePeriod))
	log.Printf("discovery: %s [%s] 指纹 %s @ %s:%d", name, platform, node.selfFingerprint, protocol.LanIDGroup, protocol.LanIDPort)
	go radioLoop()
}

func (n *discNode) addEvent(s string) {
	n.mu.Lock()
	n.events = append(n.events, s)
	if len(n.events) > 50 {
		n.events = n.events[len(n.events)-50:]
	}
	n.mu.Unlock()
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
			node.onPeerDiscovered(p)
		}
	}
}

// onPeerDiscovered：协议层验签通过、聚合成 LanIDPeer 后「告诉客户端」——记录事件、
// 并入信任链、发起 identify 握手（§4.2）。
func (n *discNode) onPeerDiscovered(p protocol.LanIDPeer) {
	n.mu.Lock()
	already := n.connected[p.Fingerprint]
	n.connected[p.Fingerprint] = true
	n.mu.Unlock()

	n.addEvent(fmt.Sprintf("发现对端 %s [%s] %s:%d 指纹 %s（验签通过）",
		p.Name, p.Platform, p.Addr, p.Port, p.Fingerprint))
	if already {
		return
	}
	_ = n.trust.Append(p.PubKey, 1) // 信任链记录：本节点见过该对端
	idPayload := &protocol.IdentifyPayload{
		Version: protocol.ProtocolVersion, Platform: n.selfPlatform, Role: protocol.RoleBoth,
		Caps: protocol.CapIntelligentLink | protocol.CapDirectLink | protocol.CapDTLS13 | protocol.CapClipboard,
		SeqInit: 0x5A5A5A5A, Name: n.selfName,
	}
	_ = idPayload.Validate()
	n.addEvent(fmt.Sprintf("→ 向 %s 发起 identify 握手（%d B；真实场景经 TCP 到 %s:%d）",
		p.Name, len(idPayload.Marshal()), p.Addr, p.Port))
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
	connected := node.connected
	node.mu.Unlock()

	out := []map[string]any{}
	for _, p := range node.listener.Peers() {
		if p.Fingerprint == node.selfFingerprint {
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
		})
	}
	writeJSON(w, map[string]any{"peers": out, "events": events})
}

// ── ② 链接：identify 握手（§4.2）+ 链路规划（PlanLinks）───────────────────

func handleLink(w http.ResponseWriter, r *http.Request) {
	steps := []string{}

	// A（Mac）→ B：IDENTIFY 载荷，打成帧上链，B 解帧 + 反序列化 + 校验
	aID := &protocol.IdentifyPayload{
		Version: protocol.ProtocolVersion, Platform: protocol.PlatformMacOS, Role: protocol.RoleBoth,
		Caps: protocol.CapIntelligentLink | protocol.CapDirectLink | protocol.CapDTLS13 | protocol.CapClipboard,
		SeqInit: 0x1A2B3C4D, Name: "Kelthas的Mac mini",
	}
	if err := aID.Validate(); err != nil {
		fail(w, err)
		return
	}
	idWire := encodeFrame(protocol.FrameIdentify, 1, aID.Marshal())
	steps = append(steps, fmt.Sprintf("Mac → Linux IDENTIFY 帧 %d B（含 8B HMAC）", len(idWire)))
	idDecoded, _, err := protocol.DecodeFrameBytesWithHMAC(idWire, demoHMACKey)
	if err != nil {
		fail(w, err)
		return
	}
	bGotID, err := protocol.UnmarshalIdentify(idDecoded.Payload)
	if err != nil {
		fail(w, err)
		return
	}
	steps = append(steps, fmt.Sprintf("Linux 校验通过：version=%d platform=%s role=%d name=%q",
		bGotID.Version, bGotID.Platform, bGotID.Role, bGotID.Name))

	// B（Linux）→ A：IDENTIFY_RESPONSE（status 0x00 接受）
	bResp := &protocol.IdentifyResponsePayload{
		Status: 0x00, Platform: protocol.PlatformLinux,
		Caps: protocol.CapIntelligentLink | protocol.CapDirectLink | protocol.CapDTLS13 | protocol.CapClipboard,
		Name: "deepin 工作站",
	}
	respWire := encodeFrame(protocol.FrameIdentifyRes, 2, bResp.Marshal())
	respDecoded, _, err := protocol.DecodeFrameBytesWithHMAC(respWire, demoHMACKey)
	if err != nil {
		fail(w, err)
		return
	}
	aGotResp, err := protocol.UnmarshalIdentifyResponse(respDecoded.Payload)
	if err != nil {
		fail(w, err)
		return
	}
	steps = append(steps, fmt.Sprintf("Mac 收到 IDENTIFY_RESPONSE：status=%d（0=接受）platform=%s name=%q",
		aGotResp.Status, aGotResp.Platform, aGotResp.Name))

	// 链路规划：智能连接优先，且两种对端都可见
	plan := protocol.PlanLinks(protocol.PreferredIntelligentFirst, 1, 1, false)
	order := []string{}
	for _, m := range plan.Order {
		order = append(order, m.String())
	}
	steps = append(steps, "PlanLinks(智能优先, mdns=1, lan=1) → 连接计划 "+fmt.Sprint(order))

	writeJSON(w, map[string]any{
		"steps":    steps,
		"accepted": aGotResp.Status == 0x00,
		"peer":     map[string]any{"name": bGotID.Name, "platform": bGotID.Platform.String(), "caps": fmt.Sprintf("0x%02x", uint8(bGotID.Caps))},
		"linkPlan": map[string]any{"order": order, "exhausted": plan.Exhausted},
	})
}

// ── ③ 剪切板：两个 ClipboardSession 经真实帧往返（§5.3）─────────────────

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

// ── ④ 命令识别：键鼠 payload 编解码往返 + 跨平台键位重映射 ────────────────

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
