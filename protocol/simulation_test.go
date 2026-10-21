package protocol

import (
	"net"
	"strings"
	"testing"
)

// TestLocalSimulation 是一次纯进程内的协议层演练，不碰任何 socket / OS 输入：
//   ① 两个服务（Mac 与 Windows）各持一个 mDNS 通告器 + 浏览器，互相把对方的
//      公告字节喂进自己的浏览器，验证彼此能发现（含查询/应答路径）；
//   ② 键鼠事件（keydown / keyup / mousemove / click / scroll）经
//      payload.Marshal → frame.EncodeBytesWithHMAC → DecodeFrameBytesWithHMAC
//      → Unmarshal 完整往返，验证协议层能承载键鼠行为；
//   ③ 跨平台键位重映射：Mac 的 Cmd(Meta) 在 Windows 上落成 Ctrl。
// 协议层本身不做 I/O，客户端只负责把字节搬进搬出（见 AGENTS.md #28）。
func TestLocalSimulation(t *testing.T) {
	t.Log("══════════ ① 两服务相互发现（mDNS，纯字节交换）══════════")

	macIP := net.ParseIP("192.168.1.10")
	winIP := net.ParseIP("192.168.1.20")

	advMac, err := NewMdnsAdvertisor(
		"Mac-mini._flowgrid._tcp.local", "macmini.local", macIP, 24801,
		MdnsTXTRecord{
			Version: "1", Platform: "macos", Name: "Kelthas的Mac mini",
			Caps: "intelligentlink,directlink,dtls,clipboard", Port: "24801",
			Encryption: "dtls13", HWAddr: "a1:b2:c3:d4:e5:f6",
		})
	if err != nil {
		t.Fatalf("NewMdnsAdvertisor(mac): %v", err)
	}
	advWin, err := NewMdnsAdvertisor(
		"FG-PC._flowgrid._tcp.local", "fgpc.local", winIP, 24801,
		MdnsTXTRecord{
			Version: "1", Platform: "windows", Name: "FG-PC",
			Caps: "intelligentlink,directlink,dtls,clipboard", Port: "24801",
			Encryption: "dtls13", HWAddr: "b2:c3:d4:e5:f6:a1",
		})
	if err != nil {
		t.Fatalf("NewMdnsAdvertisor(win): %v", err)
	}

	brMac := NewMdnsBrowser() // Mac 侧的浏览器
	brWin := NewMdnsBrowser() // Windows 侧的浏览器

	// —— 主动公告路径：A.Announce → B.Feed ——
	brWin.Feed(advMac.Announce(), macIP) // Mac 公告，Windows 浏览器收到
	brMac.Feed(advWin.Announce(), winIP) // Windows 公告，Mac 浏览器收到

	// —— 查询/应答路径：Windows 发查询 → Mac 应答 → Windows 浏览器收到 ——
	if resp := advMac.HandleQuery(BuildQueryPacket()); resp != nil {
		brWin.Feed(resp, macIP)
	}

	peersOnWin := brWin.Peers() // Windows 应看到 Mac
	peersOnMac := brMac.Peers() // Mac 应看到 Windows

	t.Logf("Windows 侧发现 %d 台：", len(peersOnWin))
	for _, p := range peersOnWin {
		t.Logf("   · %s  [%s] %s:%d  platform=%s caps=%s", p.Name, p.Instance, p.Addr, p.Port, p.Platform, p.Caps)
	}
	t.Logf("Mac 侧发现 %d 台：", len(peersOnMac))
	for _, p := range peersOnMac {
		t.Logf("   · %s  [%s] %s:%d  platform=%s caps=%s", p.Name, p.Instance, p.Addr, p.Port, p.Platform, p.Caps)
	}

	if !containsPeer(peersOnWin, "Mac-mini._flowgrid._tcp.local") {
		t.Error("Windows 未能发现 Mac")
	}
	if !containsPeer(peersOnMac, "FG-PC._flowgrid._tcp.local") {
		t.Error("Mac 未能发现 Windows")
	}
	t.Log("✔ 两服务经 mDNS 相互发现成功")

	// ══════════════════════════════════════════════════════════════
	t.Log("")
	t.Log("══════════ ② 键鼠行为端到端（payload → frame → payload）══════════")

	hmacKey := []byte("flowgrid-sim-hmac-key-32-bytes!!") // 32 B
	var seq uint16
	nextSeq := func() uint16 { seq++; return seq }

	// 把「发送侧构造的事件」经协议层打包成线上字节，再在「接收侧」解回，
	// 返回解回的事件（frame 类型 + payload 指针）。
	roundTrip := func(ft uint8, payload []byte) (*HIDFrame, error) {
		wire, err := (&HIDFrame{
			Header: FrameHeader{
				FrameType: ft, Sequence: nextSeq(), Timestamp: 1_700_000_000_000,
			},
			Payload: payload,
		}).EncodeBytesWithHMAC(hmacKey)
		if err != nil {
			return nil, err
		}
		got, _, err := DecodeFrameBytesWithHMAC(wire, hmacKey)
		return got, err
	}

	// —— 键盘：Mac 上按 Cmd+C（复制）——
	kdSend := &KeyDownPayload{KeyCode: HIDKeyC, Modifiers: ModLeftMeta}
	kdFrame, err := roundTrip(FrameKeyDown, kdSend.Marshal())
	if err != nil {
		t.Fatalf("keydown round trip: %v", err)
	}
	kdGot, err := UnmarshalKeyDown(kdFrame.Payload)
	if err != nil {
		t.Fatalf("UnmarshalKeyDown: %v", err)
	}
	t.Logf("keydown : KeyCode=%#04x Modifiers=%#04x  (Mac 按 Cmd+C)", kdGot.KeyCode, kdGot.Modifiers)
	if kdGot.KeyCode != HIDKeyC || kdGot.Modifiers != ModLeftMeta {
		t.Errorf("keydown 往返不一致: %+v", kdGot)
	}

	kuSend := &KeyUpPayload{KeyCode: HIDKeyC}
	kuFrame, err := roundTrip(FrameKeyUp, kuSend.Marshal())
	if err != nil {
		t.Fatalf("keyup round trip: %v", err)
	}
	kuGot, _ := UnmarshalKeyUp(kuFrame.Payload)
	t.Logf("keyup   : KeyCode=%#04x", kuGot.KeyCode)
	if kuGot.KeyCode != HIDKeyC {
		t.Errorf("keyup 往返不一致: %+v", kuGot)
	}

	// —— 鼠标移动 ——
	mmSend := &MouseMovePayload{DeltaX: 120, DeltaY: -45}
	mmFrame, err := roundTrip(FrameMouseMove, mmSend.Marshal())
	if err != nil {
		t.Fatalf("mousemove round trip: %v", err)
	}
	mmGot, _ := UnmarshalMouseMove(mmFrame.Payload)
	t.Logf("mousemove: ΔX=%d ΔY=%d", mmGot.DeltaX, mmGot.DeltaY)
	if mmGot.DeltaX != 120 || mmGot.DeltaY != -45 {
		t.Errorf("mousemove 往返不一致: %+v", mmGot)
	}

	// —— 鼠标左键按下 ——
	mbSend := &MouseButtonPayload{ButtonID: MouseButtonLeft, State: 0x01}
	mbFrame, err := roundTrip(FrameMouseButton, mbSend.Marshal())
	if err != nil {
		t.Fatalf("mousebutton round trip: %v", err)
	}
	mbGot, _ := UnmarshalMouseButton(mbFrame.Payload)
	t.Logf("mousebtn : Button=%d State=%d (左键按下)", mbGot.ButtonID, mbGot.State)
	if mbGot.ButtonID != MouseButtonLeft || mbGot.State != 0x01 {
		t.Errorf("mousebutton 往返不一致: %+v", mbGot)
	}

	// —— 滚轮：向上滚 3 格 ——
	scSend := &ScrollPayload{DeltaY: 3, DeltaX: 0}
	scFrame, err := roundTrip(FrameScroll, scSend.Marshal())
	if err != nil {
		t.Fatalf("scroll round trip: %v", err)
	}
	scGot, _ := UnmarshalScroll(scFrame.Payload)
	t.Logf("scroll   : ΔY=%d ΔX=%d (向上 3 格)", scGot.DeltaY, scGot.DeltaX)
	if scGot.DeltaY != 3 || scGot.DeltaX != 0 {
		t.Errorf("scroll 往返不一致: %+v", scGot)
	}
	t.Log("✔ 键鼠事件经协议层完整往返，无失真")

	// ══════════════════════════════════════════════════════════════
	t.Log("")
	t.Log("══════════ ③ 跨平台键位重映射（Mac Cmd → Windows Ctrl）══════════")

	// Mac 用 Meta(Cmd) 作主修饰键，Windows 用 Ctrl。同一次「复制」快捷键，
	// 在 Windows 上必须落成 Ctrl+C，否则用户按下去没反应。
	remappedMods := RemapModifiers(ModLeftMeta, PlatformMacOS, PlatformWindows)
	remappedKey := RemapModifierKey(HIDKeyLMeta, PlatformMacOS, PlatformWindows)
	t.Logf("修饰掩码 : Mac %#04x(Meta) → Windows %#04x(Ctrl)", ModLeftMeta, remappedMods)
	t.Logf("修饰键码 : Mac %#04x(LMeta) → Windows %#04x(LCtrl)", HIDKeyLMeta, remappedKey)
	if remappedMods != ModLeftCtrl {
		t.Errorf("Cmd 掩码应重映射为 Ctrl(%#04x)，得到 %#04x", ModLeftCtrl, remappedMods)
	}
	if remappedKey != HIDKeyLCtrl {
		t.Errorf("LMeta 键码应重映射为 LCtrl(%#04x)，得到 %#04x", HIDKeyLCtrl, remappedKey)
	}

	// 同平台不重映射（恒等）
	if RemapModifiers(ModLeftMeta, PlatformMacOS, PlatformMacOS) != ModLeftMeta {
		t.Error("同平台修饰掩码不应被改写")
	}
	t.Log("✔ 跨平台键位重映射正确：Mac 的 Cmd 落到 Windows 的 Ctrl")

	t.Log("")
	t.Log("══════════ 本地模拟完成：发现 + 键鼠均经协议层跑通 ══════════")
}

func containsPeer(peers []DiscoveredPeer, instance string) bool {
	for _, p := range peers {
		if strings.TrimSuffix(p.Instance, ".") == strings.TrimSuffix(instance, ".") {
			return true
		}
	}
	return false
}
