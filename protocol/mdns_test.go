package protocol

import (
	"encoding/binary"
	"net"
	"testing"
	"time"
)

// buildResponsePacket 手工拼一条应答，记录顺序与真实 mDNS responder 一致：
// A 在前（host 名），PTR → SRV → TXT（实例名）。SRV 的 rdata 用压缩指针
// 回指 A 记录的 owner，用来压 parseName 的指针分支。
func buildResponsePacket(t *testing.T, instance, host string, port uint16, ip net.IP, txt []string) []byte {
	t.Helper()
	ptr := func(off int) []byte { return binary.BigEndian.AppendUint16(nil, uint16(off)|0xC000) }

	pkt := make([]byte, 0, 512)
	pkt = binary.BigEndian.AppendUint16(pkt, 0)
	pkt = binary.BigEndian.AppendUint16(pkt, dnsFlagResponse)
	pkt = binary.BigEndian.AppendUint16(pkt, 0)
	pkt = binary.BigEndian.AppendUint16(pkt, 4) // ancount
	pkt = binary.BigEndian.AppendUint16(pkt, 0)
	pkt = binary.BigEndian.AppendUint16(pkt, 0)

	record := func(name []byte, rtype uint16, rdata []byte) {
		pkt = append(pkt, name...)
		pkt = binary.BigEndian.AppendUint16(pkt, rtype)
		pkt = binary.BigEndian.AppendUint16(pkt, dnsClassIN|dnsClassCacheFlsh)
		pkt = binary.BigEndian.AppendUint32(pkt, 120)
		pkt = binary.BigEndian.AppendUint16(pkt, uint16(len(rdata)))
		pkt = append(pkt, rdata...)
	}

	// A 记录放最前，SRV 之后就能用指针指回来。
	hostNameOff := len(pkt)
	record(appendName(nil, host), dnsTypeA, ip.To4())

	record(appendName(nil, mdnsService), dnsTypePTR, appendName(nil, instance))

	srvRD := []byte{0, 0, 0, 0}
	srvRD = binary.BigEndian.AppendUint16(srvRD, port)
	srvRD = append(srvRD, ptr(hostNameOff)...)
	record(appendName(nil, instance), dnsTypeSRV, srvRD)

	var txtRD []byte
	for _, t := range txt {
		txtRD = append(txtRD, byte(len(t)))
		txtRD = append(txtRD, t...)
	}
	if len(txtRD) == 0 {
		txtRD = []byte{0}
	}
	record(appendName(nil, instance), dnsTypeTXT, txtRD)

	return pkt
}

func TestParseNameFollowsCompressionPointer(t *testing.T) {
	// "hi" 后面接一个指向 offset 5 的指针，那里放着另一个名字 "na" 的长度字节。
	// 期望结果是两段拼接，且 next 停在指针之后（指针占 2 字节）。
	buf := []byte{2, 'h', 'i', 0xC0, 0x05, 2, 'n', 'a', 0}
	name, next, err := parseName(buf, 0, 0)
	if err != nil {
		t.Fatalf("parseName: %v", err)
	}
	if name != "hi.na." {
		t.Fatalf("得到 %q，期望「已读标签 + 指针所指名」", name)
	}
	if next != 5 {
		t.Fatalf("指针后 next 应为 5（指针占 2 字节），实际 %d", next)
	}
}

func TestParseNameRejectsSelfPointerLoop(t *testing.T) {
	// 指针指向自己：解析器必须终止，而不是死循环。
	buf := []byte{0xC0, 0x00}
	if _, _, err := parseName(buf, 0, 0); err == nil {
		t.Fatal("自指压缩指针应当被拒绝")
	}
}

func TestMdnsBrowserHandlesCompressedResponse(t *testing.T) {
	instance := "MacBook Pro._flowgrid._tcp.local."
	pkt := buildResponsePacket(t, instance, "host.local.", 24801, net.IPv4(192, 168, 1, 20),
		[]string{"version=1", "platform=macos", "name=MacBook Pro",
			"caps=directlink,clipboard", "port=24801", "encryption=dtls13"})

	b := NewMdnsBrowser()
	b.handlePacket(pkt, &net.UDPAddr{IP: net.IPv4(192, 168, 1, 20), Port: 5353})

	got := b.Poll()
	if len(got) != 1 {
		t.Fatalf("发现 %d 台设备，期望 1 台", len(got))
	}
	d := got[0]
	if d.Name != "MacBook Pro" || d.Port != 24801 {
		t.Fatalf("设备信息不对：name=%q port=%d", d.Name, d.Port)
	}
	if !d.Addr.Equal(net.IPv4(192, 168, 1, 20)) {
		t.Fatalf("地址不对：%v", d.Addr)
	}
	if d.Platform != "macos" || d.Encryption != "dtls13" {
		t.Fatalf("TXT 解析不对：platform=%q encryption=%q", d.Platform, d.Encryption)
	}
}

func TestMdnsBrowserPollIsDraining(t *testing.T) {
	instance := "a._flowgrid._tcp.local."
	pkt := buildResponsePacket(t, instance, "h.local.", 24801, net.IPv4(10, 0, 0, 5), nil)
	b := NewMdnsBrowser()
	b.handlePacket(pkt, &net.UDPAddr{IP: net.IPv4(10, 0, 0, 5), Port: 5353})

	if n := len(b.Poll()); n != 1 {
		t.Fatalf("首次 Poll 应得 1 条，实际 %d", n)
	}
	if n := len(b.Poll()); n != 0 {
		t.Fatalf("Poll 应是取走即出队，二次应为 0，实际 %d", n)
	}
	if n := len(b.Peers()); n != 1 {
		t.Fatalf("已知设备表应保留 1 条，实际 %d", n)
	}
}

func TestMdnsBrowserIgnoresForeignService(t *testing.T) {
	// 别人的 _ipp._tcp 通告不能被当成 FlowGrid 设备。
	pkt := make([]byte, 0, 64)
	pkt = binary.BigEndian.AppendUint16(pkt, 0)
	pkt = binary.BigEndian.AppendUint16(pkt, dnsFlagResponse)
	pkt = binary.BigEndian.AppendUint16(pkt, 0)
	pkt = binary.BigEndian.AppendUint16(pkt, 1)
	pkt = binary.BigEndian.AppendUint16(pkt, 0)
	pkt = binary.BigEndian.AppendUint16(pkt, 0)
	pkt = appendName(pkt, "_ipp._tcp.local.")
	pkt = binary.BigEndian.AppendUint16(pkt, dnsTypePTR)
	pkt = binary.BigEndian.AppendUint16(pkt, dnsClassIN)
	pkt = binary.BigEndian.AppendUint32(pkt, 120)
	rd := appendName(nil, "printer._ipp._tcp.local.")
	pkt = binary.BigEndian.AppendUint16(pkt, uint16(len(rd)))
	pkt = append(pkt, rd...)

	b := NewMdnsBrowser()
	b.handlePacket(pkt, &net.UDPAddr{IP: net.IPv4(10, 0, 0, 9), Port: 5353})
	if n := len(b.Peers()); n != 0 {
		t.Fatalf("非 FlowGrid 服务不应被发现，实际 %d 条", n)
	}
}

func TestMdnsBrowserIgnoresQueryPacket(t *testing.T) {
	// 我们自己发出的查询会被自己收到，不能当成应答。
	b := NewMdnsBrowser()
	b.handlePacket(buildQueryPacket(), &net.UDPAddr{IP: net.IPv4(10, 0, 0, 5), Port: 5353})
	if n := len(b.Peers()); n != 0 {
		t.Fatalf("查询包不应产生设备，实际 %d 条", n)
	}
}

func TestMdnsBrowserPrune(t *testing.T) {
	instance := "b._flowgrid._tcp.local."
	pkt := buildResponsePacket(t, instance, "h.local.", 24801, net.IPv4(10, 0, 0, 7), nil)
	b := NewMdnsBrowser()
	b.handlePacket(pkt, &net.UDPAddr{IP: net.IPv4(10, 0, 0, 7), Port: 5353})

	if n := b.Prune(time.Minute); n != 0 {
		t.Fatalf("刚发现的设备不该被淘汰，淘汰了 %d 台", n)
	}
	if n := b.Prune(0); n != 1 {
		t.Fatalf("过期设备应被淘汰 1 台，实际 %d", n)
	}
	if n := len(b.Peers()); n != 0 {
		t.Fatalf("淘汰后设备表应为空，实际 %d", n)
	}
}

// Start 起了两个循环，Stop 必须等两个都退干净。曾经只 Add(1) 却有两个
// Done——计数掉到 -1，后收尾的循环在 Done 里 panic，Stop 直接把进程带走。
// 这个不靠组播就能测：组播不可用（容器 / 隔离网段）时 Start 返回错误，跳过。
func TestMdnsBrowserStopBalancesWaitGroup(t *testing.T) {
	b := NewMdnsBrowser()
	if err := b.Start(); err != nil {
		t.Skipf("当前网络不支持组播：%v", err)
	}

	stopped := make(chan struct{})
	go func() {
		b.Stop()
		close(stopped)
	}()
	select {
	case <-stopped:
	case <-time.After(5 * time.Second):
		t.Fatal("Stop 未在两个循环退出后返回")
	}

	// 留一拍给两个循环各自的 defer：计数若不平衡，第二个 Done 会在这里
	// panic，测试进程以非零状态退出，而不是悄悄放过。
	time.Sleep(200 * time.Millisecond)
}

func TestMdnsAdvertisorRoundTrip(t *testing.T) {
	txt := MdnsTXTRecord{
		Version: "1", Platform: "linux", Name: "deepin 工作站",
		Caps: "directlink,clipboard", Port: "24801",
		Encryption: "dtls13", HWAddr: "aa:bb:cc:dd:ee:ff",
	}
	adv, err := NewMdnsAdvertisor("deepin 工作站._flowgrid._tcp.local.",
		"deepin-workstation.local.", net.IPv4(192, 168, 1, 30), 24801, txt)
	if err != nil {
		t.Skipf("当前网络不支持组播：%v", err)
	}
	defer adv.Stop()

	pkt := make([]byte, 0, 512)
	pkt = binary.BigEndian.AppendUint16(pkt, 0)
	pkt = binary.BigEndian.AppendUint16(pkt, dnsFlagResponse)
	pkt = binary.BigEndian.AppendUint16(pkt, 0)
	pkt = binary.BigEndian.AppendUint16(pkt, uint16(len(adv.records)))
	pkt = binary.BigEndian.AppendUint16(pkt, 0)
	pkt = binary.BigEndian.AppendUint16(pkt, 0)
	for _, rec := range adv.records {
		pkt = append(pkt, rec.marshal()...)
	}

	b := NewMdnsBrowser()
	b.handlePacket(pkt, &net.UDPAddr{IP: net.IPv4(192, 168, 1, 30), Port: 5353})

	got := b.Poll()
	if len(got) != 1 {
		t.Fatalf("解析自己的通告失败，发现 %d 台", len(got))
	}
	d := got[0]
	if d.Name != "deepin 工作站" || d.Port != 24801 {
		t.Fatalf("通告解析不对：%q / %d", d.Name, d.Port)
	}
	if !d.Addr.Equal(net.IPv4(192, 168, 1, 30)) {
		t.Fatalf("A 记录不对：%v", d.Addr)
	}
	if d.HWAddr != "aa:bb:cc:dd:ee:ff" {
		t.Fatalf("hwaddr 解析不对：%q", d.HWAddr)
	}
}

func TestTxtValidateRejectsMissingFields(t *testing.T) {
	cases := []MdnsTXTRecord{
		{Platform: "linux", Name: "x", Port: "1"},
		{Version: "1", Name: "x", Port: "1"},
		{Version: "1", Platform: "linux", Port: "1"},
		{Version: "1", Platform: "linux", Name: "x"},
		{Version: "1", Platform: "linux", Name: "x", Port: "abc"},
	}
	for i, c := range cases {
		if err := c.Validate(); err == nil {
			t.Fatalf("用例 %d：残缺 TXT 应当被拒绝 %+v", i, c)
		}
	}
	ok := MdnsTXTRecord{Version: "1", Platform: "linux", Name: "x", Port: "24801"}
	if err := ok.Validate(); err != nil {
		t.Fatalf("完整 TXT 应通过：%v", err)
	}
}
