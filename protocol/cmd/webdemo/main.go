// Command webdemo 是一个纯协议层的 web 演示：用浏览器页面驱动 Go 协议库，
// 在进程内演示四个能力——设备发现（mDNS 两实例互发现）、链接（identify 握手 +
// 链路规划）、剪切板（两 ClipboardSession 经真实帧往返）、命令识别（键鼠 payload
// 编解码 + 跨平台键位重映射）。
//
// 注意：只调 protocol 包，不含 transport（UDP socket）、HAL（输入注入）、GUI。
// 两实例的「网络」是进程内字节交换（Advertiser.Announce → Browser.Feed 等），
// 正是协议层该负责的部分（AGENTS.md #28）。
package main

import (
	"embed"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"net"
	"net/http"
	"os"

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
	http.HandleFunc("/", serveIndex)
	http.HandleFunc("/api/discover", handleDiscover)
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

// ── ① 设备发现：两个 mDNS 实例（Mac + Linux）互相发现 ──────────────────

func handleDiscover(w http.ResponseWriter, r *http.Request) {
	macIP := net.ParseIP("192.168.1.10")
	linuxIP := net.ParseIP("192.168.1.20")

	advMac, err := protocol.NewMdnsAdvertisor("Mac-mini._flowgrid._tcp.local", "macmini.local", macIP, 24801,
		protocol.MdnsTXTRecord{Version: "1", Platform: "macos", Name: "Kelthas的Mac mini",
			Caps: "intelligentlink,directlink,dtls,clipboard,file", Port: "24801", Encryption: "dtls13", HWAddr: "a1:b2:c3:d4:e5:f6"})
	if err != nil {
		fail(w, err)
		return
	}
	advLinux, err := protocol.NewMdnsAdvertisor("deepin-PC._flowgrid._tcp.local", "deepin.local", linuxIP, 24801,
		protocol.MdnsTXTRecord{Version: "1", Platform: "linux", Name: "deepin 工作站",
			Caps: "intelligentlink,directlink,dtls,clipboard,file", Port: "24801", Encryption: "dtls13", HWAddr: "b2:c3:d4:e5:f6:a1"})
	if err != nil {
		fail(w, err)
		return
	}
	brMac := protocol.NewMdnsBrowser()
	brLinux := protocol.NewMdnsBrowser()

	steps := []string{}
	// 主动公告：A.Announce → B.Feed
	macAdv := advMac.Announce()
	brLinux.Feed(macAdv, macIP)
	steps = append(steps, fmt.Sprintf("Mac 公告 %d B → Linux 浏览器 Feed", len(macAdv)))
	linuxAdv := advLinux.Announce()
	brMac.Feed(linuxAdv, linuxIP)
	steps = append(steps, fmt.Sprintf("Linux 公告 %d B → Mac 浏览器 Feed", len(linuxAdv)))
	// 查询/应答：Linux 发查询 → Mac 应答 → Linux 浏览器 Feed
	query := protocol.BuildQueryPacket()
	if resp := advMac.HandleQuery(query); resp != nil {
		brLinux.Feed(resp, macIP)
		steps = append(steps, fmt.Sprintf("Linux 查询 %d B → Mac 应答 %d B → Linux 浏览器", len(query), len(resp)))
	}

	writeJSON(w, map[string]any{
		"steps":    steps,
		"onLinux":  peersToJSON(brLinux.Peers()),
		"onMac":    peersToJSON(brMac.Peers()),
	})
}

func peersToJSON(peers []protocol.DiscoveredPeer) []map[string]any {
	out := []map[string]any{}
	for _, p := range peers {
		out = append(out, map[string]any{
			"name":       p.Name,
			"instance":   p.Instance,
			"addr":       p.Addr.String(),
			"port":       p.Port,
			"platform":   p.Platform,
			"caps":       p.Caps,
			"encryption": p.Encryption,
		})
	}
	return out
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
