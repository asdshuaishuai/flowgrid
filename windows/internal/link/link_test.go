package link

import (
	"net"
	"path/filepath"
	"testing"
	"time"

	"github.com/flowgrid/protocol"
)

func testConfig(name string, autoAccept bool) Config {
	return Config{
		Name:       name,
		Port:       0, // 监听 :0 由内核挑端口，测试经 Addr() 取实际值
		AutoAccept: autoAccept,
		Platform:   protocol.PlatformWindows,
		Caps:       protocol.CapDirectLink | protocol.CapClipboard,
	}
}

func loopResolver(port int) Resolver {
	return func(string) (net.IP, int, bool) {
		return net.IPv4(127, 0, 0, 1), port, true
	}
}

func waitFor(t *testing.T, what string, timeout time.Duration, ok func() bool) {
	t.Helper()
	deadline := time.Now().Add(timeout)
	for time.Now().Before(deadline) {
		if ok() {
			return
		}
		time.Sleep(20 * time.Millisecond)
	}
	t.Fatalf("等待 %s 超时（%v）", what, timeout)
}

func TestTrustStorePersist(t *testing.T) {
	dir := t.TempDir()
	trust, err := OpenTrustStore(dir)
	if err != nil {
		t.Fatalf("open: %v", err)
	}
	if trust.IsTrusted("ab12") {
		t.Fatal("empty store must have no trust entries")
	}
	if err := trust.Add("ab12", "Beta", []byte("fake-der")); err != nil {
		t.Fatalf("add: %v", err)
	}
	reopened, err := OpenTrustStore(dir)
	if err != nil {
		t.Fatalf("reopen: %v", err)
	}
	if !reopened.IsTrusted("ab12") || reopened.NameOf("ab12") != "Beta" {
		t.Fatalf("trust entry must persist: %v", reopened.entries)
	}
	if string(reopened.CertByName("Beta")) != "fake-der" {
		t.Fatalf("pinned certificate must round-trip: %q", reopened.CertByName("Beta"))
	}
	if reopened.CertByName("nobody") != nil {
		t.Fatal("unknown name must return nil certificate")
	}
}

func TestIdentityStableAcrossReload(t *testing.T) {
	dir := t.TempDir()
	id1, err := LoadOrCreate(dir)
	if err != nil {
		t.Fatalf("create: %v", err)
	}
	if len(id1.Fingerprint) != 32 {
		t.Fatalf("指纹应为 32 个十六进制字符：%q", id1.Fingerprint)
	}
	id2, err := LoadOrCreate(dir)
	if err != nil {
		t.Fatalf("reload: %v", err)
	}
	if id1.Fingerprint != id2.Fingerprint {
		t.Fatalf("重载后指纹变化：%s != %s", id1.Fingerprint, id2.Fingerprint)
	}
}

// 全链路环回：B 拨 A，双向 IDENTIFY（双方都 auto-accept）→ 在线 → 心跳/延迟
// 跑起来 → 用户断开 → 双侧链接消失。
func TestEngineLoopbackHandshake(t *testing.T) {
	idA, err := LoadOrCreate(t.TempDir())
	if err != nil {
		t.Fatal(err)
	}
	idB, err := LoadOrCreate(t.TempDir())
	if err != nil {
		t.Fatal(err)
	}
	trustA, _ := OpenTrustStore(filepath.Join(t.TempDir(), "t"))
	trustB, _ := OpenTrustStore(filepath.Join(t.TempDir(), "t"))

	engA := New(testConfig("Alpha", true), idA, trustA, nil)
	if err := engA.Listen(); err != nil {
		t.Fatalf("listen: %v", err)
	}
	defer engA.Stop()
	portA := engA.Addr().(*net.TCPAddr).Port

	engB := New(testConfig("Beta", true), idB, trustB, loopResolver(portA))

	// 拨号方的固定根 = 已配对对端的证书（正式流程由配对时入库）。
	if err := trustB.Add(idA.Fingerprint, "Alpha", idA.Cert.Certificate[0]); err != nil {
		t.Fatalf("seed trust: %v", err)
	}
	if err := engB.Connect(InstanceOf("Alpha")); err != nil {
		t.Fatalf("connect: %v", err)
	}
	waitFor(t, "A 侧 Beta 在线", 5*time.Second, func() bool {
		for _, s := range engA.Snapshots() {
			if s.Name == "Beta" && s.State == string(StateOnline) {
				return true
			}
		}
		return false
	})
	waitFor(t, "B 侧 Alpha 在线", 5*time.Second, func() bool {
		for _, s := range engB.Snapshots() {
			if s.Name == "Alpha" && s.State == string(StateOnline) {
				return true
			}
		}
		return false
	})

	// 心跳与延迟采样已跑起来：轮询等待首个样本（首个 tick 在 +2s，留裕量）。
	// 注意断言查样本计数而非值——环回 RTT 可为 0µs，合法样本值为 0。
	latDeadline := time.Now().Add(6 * time.Second)
	latOK := false
	for time.Now().Before(latDeadline) {
		for _, s := range engB.Snapshots() {
			if s.Name == "Alpha" && s.Samples > 0 {
				latOK = true
			}
		}
		if latOK {
			break
		}
		time.Sleep(100 * time.Millisecond)
	}
	t.Logf("latency-flake A=%+v", engA.Snapshots())
	t.Logf("latency-flake B=%+v", engB.Snapshots())
	if !latOK {
		t.Fatal("6s 内无延迟样本")
	}

	// B 侧用户断开 Alpha：两侧链接都应消失（B 不再重连）。
	if !engB.Disconnect(InstanceOf("Alpha")) {
		t.Fatal("Disconnect 应找到在线链接")
	}
	waitFor(t, "断开后 A 侧链接消失", 5*time.Second, func() bool {
		return len(engA.Snapshots()) == 0
	})
	waitFor(t, "断开后 B 侧链接消失", 5*time.Second, func() bool {
		return len(engB.Snapshots()) == 0
	})
}

// 确认门：B（auto-accept）拨未信任 A？反了——未信任的是 A 视角的 B。
// A 不开 auto-accept：B 拨入后 A 应出现 pending 并回 0x03，裁决接受后双向在线，
// 且 B 的指纹入库。
func TestEnginePairConfirmGate(t *testing.T) {
	idA, _ := LoadOrCreate(t.TempDir())
	idB, _ := LoadOrCreate(t.TempDir())
	trustA, _ := OpenTrustStore(t.TempDir())
	trustB, _ := OpenTrustStore(t.TempDir())

	engA := New(testConfig("Alpha", false), idA, trustA, nil)
	if err := engA.Listen(); err != nil {
		t.Fatalf("listen: %v", err)
	}
	defer engA.Stop()
	portA := engA.Addr().(*net.TCPAddr).Port

	engB := New(testConfig("Beta", true), idB, trustB, loopResolver(portA))
	defer engB.Stop()
	if err := trustB.Add(idA.Fingerprint, "Alpha", idA.Cert.Certificate[0]); err != nil {
		t.Fatalf("seed trust: %v", err)
	}
	if err := engB.Connect(InstanceOf("Alpha")); err != nil {
		t.Fatalf("connect: %v", err)
	}

	waitFor(t, "A 侧出现待确认配对", 5*time.Second, func() bool {
		return len(engA.PendingPairs()) == 1
	})
	pending := engA.PendingPairs()[0]
	if pending.Name != "Beta" {
		t.Fatalf("pending 对端名应为 Beta，实际 %q", pending.Name)
	}
	if !engA.ResolvePair(pending.ID, true) {
		t.Fatal("裁决 pending 失败")
	}

	waitFor(t, "裁决后 A 侧 Beta 在线", 5*time.Second, func() bool {
		for _, s := range engA.Snapshots() {
			if s.Name == "Beta" && s.State == string(StateOnline) {
				return true
			}
		}
		return false
	})
	waitFor(t, "裁决后 B 侧 Alpha 在线", 5*time.Second, func() bool {
		for _, s := range engB.Snapshots() {
			if s.Name == "Alpha" && s.State == string(StateOnline) {
				return true
			}
		}
		return false
	})
	if !trustA.IsTrusted(pending.Fingerprint) {
		t.Fatal("接受后对端指纹应入库")
	}
}

// 剪贴板环回：B 发送 → A 重组完成回调，内容逐字节一致（§5.3 分段/重组）。
func TestEngineClipboardLoopback(t *testing.T) {
	idA, _ := LoadOrCreate(t.TempDir())
	idB, _ := LoadOrCreate(t.TempDir())
	trustA, _ := OpenTrustStore(t.TempDir())
	trustB, _ := OpenTrustStore(t.TempDir())

	engA := New(testConfig("Alpha", true), idA, trustA, nil)
	if err := engA.Listen(); err != nil {
		t.Fatalf("listen: %v", err)
	}
	defer engA.Stop()
	portA := engA.Addr().(*net.TCPAddr).Port

	got := make(chan protocol.Clipboard, 1)
	engA.SetHandlers(nil, func(peer string, c protocol.Clipboard) {
		select {
		case got <- c:
		default:
		}
	}, nil, nil, nil, nil)

	engB := New(testConfig("Beta", true), idB, trustB, loopResolver(portA))
	defer engB.Stop()
	if err := trustB.Add(idA.Fingerprint, "Alpha", idA.Cert.Certificate[0]); err != nil {
		t.Fatal(err)
	}
	if err := engB.Connect(InstanceOf("Alpha")); err != nil {
		t.Fatal(err)
	}
	waitFor(t, "B 侧 Alpha 在线", 5*time.Second, func() bool {
		for _, s := range engB.Snapshots() {
			if s.Name == "Alpha" && s.State == string(StateOnline) {
				return true
			}
		}
		return false
	})

	// 60KB：协议上限 63240（§5.3）以内、跨 ~240 个分片，验证分段/重组全路径。
	// 更大的传输语义上属于文件（§7.4），剪贴板超限被协议拒绝是正确行为。
	payload := make([]byte, 60000)
	for i := range payload {
		payload[i] = byte(i % 251)
	}
	if err := engB.SendClipboardTo("Alpha", "text/plain", payload); err != nil {
		t.Fatalf("send clipboard: %v", err)
	}
	select {
	case c := <-got:
		if c.MIMEType != "text/plain" || string(c.Data) != string(payload) {
			t.Fatalf("剪贴板内容不一致：mime=%q len=%d", c.MIMEType, len(c.Data))
		}
	case <-time.After(10 * time.Second):
		t.Fatal("剪贴板重组超时")
	}
}

// 文件环回：B 发送 → A 自动接受 + SHA-256 校验 + OnComplete 交付，内容一致（§7.4）。
func TestEngineFileLoopback(t *testing.T) {
	idA, _ := LoadOrCreate(t.TempDir())
	idB, _ := LoadOrCreate(t.TempDir())
	trustA, _ := OpenTrustStore(t.TempDir())
	trustB, _ := OpenTrustStore(t.TempDir())

	engA := New(testConfig("Alpha", true), idA, trustA, nil)
	if err := engA.Listen(); err != nil {
		t.Fatalf("listen: %v", err)
	}
	defer engA.Stop()
	portA := engA.Addr().(*net.TCPAddr).Port

	type delivered struct {
		offer protocol.FileOffer
		data  []byte
		err   error
	}
	done := make(chan delivered, 1)
	engA.SetHandlers(nil, nil, nil, nil, nil,
		func(peer string, offer protocol.FileOffer, data []byte, err error) {
			select {
			case done <- delivered{offer, data, err}:
			default:
			}
		})

	engB := New(testConfig("Beta", true), idB, trustB, loopResolver(portA))
	defer engB.Stop()
	if err := trustB.Add(idA.Fingerprint, "Alpha", idA.Cert.Certificate[0]); err != nil {
		t.Fatal(err)
	}
	if err := engB.Connect(InstanceOf("Alpha")); err != nil {
		t.Fatal(err)
	}
	waitFor(t, "B 侧 Alpha 在线", 5*time.Second, func() bool {
		for _, s := range engB.Snapshots() {
			if s.Name == "Alpha" && s.State == string(StateOnline) {
				return true
			}
		}
		return false
	})

	payload := make([]byte, 512*1024)
	for i := range payload {
		payload[i] = byte(i * 7)
	}
	if err := engB.SendFileToPeer("Alpha", "probe.bin", payload); err != nil {
		t.Fatalf("send file: %v", err)
	}
	select {
	case d := <-done:
		if d.err != nil {
			t.Fatalf("文件传输失败：%v", d.err)
		}
		if d.offer.Name != "probe.bin" || string(d.data) != string(payload) {
			t.Fatalf("文件内容不一致：name=%q len=%d", d.offer.Name, len(d.data))
		}
	case <-time.After(15 * time.Second):
		t.Fatal("文件传输超时")
	}
}

// 未裁决的 pending 30 秒超时按拒绝——测试不等 30s，直接验证裁决-拒绝路径。
func TestEnginePairReject(t *testing.T) {
	idA, _ := LoadOrCreate(t.TempDir())
	idB, _ := LoadOrCreate(t.TempDir())
	trustA, _ := OpenTrustStore(t.TempDir())
	trustB, _ := OpenTrustStore(t.TempDir())

	engA := New(testConfig("Alpha", false), idA, trustA, nil)
	if err := engA.Listen(); err != nil {
		t.Fatalf("listen: %v", err)
	}
	defer engA.Stop()
	portA := engA.Addr().(*net.TCPAddr).Port

	engB := New(testConfig("Beta", true), idB, trustB, loopResolver(portA))
	defer engB.Stop()
	if err := trustB.Add(idA.Fingerprint, "Alpha", idA.Cert.Certificate[0]); err != nil {
		t.Fatalf("seed trust: %v", err)
	}
	if err := engB.Connect(InstanceOf("Alpha")); err != nil {
		t.Fatalf("connect: %v", err)
	}

	waitFor(t, "A 侧出现待确认配对", 5*time.Second, func() bool {
		return len(engA.PendingPairs()) == 1
	})
	pending := engA.PendingPairs()[0]
	if !engA.ResolvePair(pending.ID, false) {
		t.Fatal("拒绝裁决失败")
	}
	waitFor(t, "拒绝后 A 侧链接被清掉", 5*time.Second, func() bool {
		return len(engA.Snapshots()) == 0
	})
	if trustA.IsTrusted(pending.Fingerprint) {
		t.Fatal("拒绝后指纹不应入库")
	}
}
