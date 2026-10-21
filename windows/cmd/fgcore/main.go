// fgcore：FlowGrid Windows 会话核心。
// mDNS 发现 + 自通告 + DirectLink TCP/TLS 连接引擎 + 命名管道 IPC。
// 输入注入与剪贴板/文件接线后续并入本进程；SYSTEM 服务（fgcored）另行立项。
package main

import (
	"crypto/x509"
	"encoding/pem"
	"errors"
	"flag"
	"log/slog"
	"net"
	"os"
	"os/signal"
	"path/filepath"
	"strconv"
	"strings"
	"syscall"

	"github.com/flowgrid/protocol"

	"github.com/flowgrid/windows/internal/core"
	"github.com/flowgrid/windows/internal/hub"
	"github.com/flowgrid/windows/internal/link"
	"github.com/flowgrid/windows/internal/mdnslink"
)

const defaultPipe = `\\.\pipe\flowgrid-core`

func main() {
	pipeName := flag.String("pipe", defaultPipe, "IPC 命名管道路径")
	name := flag.String("name", defaultName(), "本机设备名（IDENTIFY/mDNS TXT）")
	port := flag.Int("port", 24801, "DirectLink TCP 数据端口（§7.2）")
	dataDir := flag.String("data", defaultDataDir(), "身份/信任库数据目录")
	autoAccept := flag.Bool("auto-accept", false, "自动信任并接受未配对对端（仅开发/无人值守）")
	peerCert := flag.String("peer-cert", "", "导入对端证书 PEM（带外交换，首次配对的临时引导）")
	peerName := flag.String("peer-name", "", "-peer-cert 对应的对端设备名")
	flag.Parse()

	slog.SetDefault(slog.New(slog.NewTextHandler(os.Stderr, nil)))

	identity, err := link.LoadOrCreate(*dataDir)
	if err != nil {
		slog.Error("identity load failed", "err", err)
		os.Exit(1)
	}
	trust, err := link.OpenTrustStore(*dataDir)
	if err != nil {
		slog.Error("trust store open failed", "err", err)
		os.Exit(1)
	}
	if *peerCert != "" {
		if err := importPeerCert(trust, *peerCert, *peerName); err != nil {
			slog.Error("peer cert import failed", "err", err)
			os.Exit(1)
		}
		slog.Info("peer certificate imported", "name", *peerName)
	}

	// 发现表：mDNS 浏览（被动收 + 主动查）
	c := core.New()
	instance := link.InstanceOf(*name)
	c.SetSelfInstance(instance) // 自通告会被自己的浏览器收到
	c.Start()

	// 连接引擎：TCP/TLS 监听 + 拨号；地址经发现表解析
	engine := link.New(link.Config{
		Name:       *name,
		Port:       *port,
		AutoAccept: *autoAccept,
		Platform:   protocol.PlatformWindows,
		Caps:       protocol.CapIntelligentLink | protocol.CapDirectLink | protocol.CapClipboard | protocol.CapFileTransfer,
	}, identity, trust, resolveVia(c))
	if err := engine.Listen(); err != nil {
		slog.Error("link listen failed", "err", err)
		os.Exit(1)
	}

	// 自通告：让局域网对端发现自己（组播不可用只降级，不致命）
	adv, advErr := startAdvertiser(*name, *port)
	if advErr != nil {
		slog.Warn("mdns advertiser unavailable", "err", advErr)
	}

	// mDNS socket：驱动 browser（查询/应答聚合）与 advertiser（应答）
	mdnsLink := mdnslink.NewLink(c.Browser(), adv)
	if err := mdnsLink.Start(); err != nil {
		slog.Warn("mdns socket unavailable", "err", err)
		c.SetMdnsErr(err)
	}

	// 输入/剪贴板/文件中枢：需要交互桌面（钩子/剪贴板窗），失败则降级为
	// 纯连接节点（对端帧就地消化，键鼠/剪贴板/文件不可用）。
	hub := hub.New(engine,
		filepath.Join(*dataDir, "downloads"),
		filepath.Join(defaultAppData(), "FlowGrid", "keymap.yaml"))
	if err := hub.Start(); err != nil {
		slog.Warn("input hub degraded (no injection/capture/clipboard/files)", "err", err)
	}

	// IPC
	ln, err := core.ListenPipe(*pipeName)
	if err != nil {
		slog.Error("listen pipe failed", "pipe", *pipeName, "err", err)
		os.Exit(1)
	}
	srv := core.Serve(ln, c, engine, hub)
	engine.OnChange(srv.BroadcastLinkState)
	hub.OnChange(srv.BroadcastInputState)
	slog.Info("fgcore up", "pipe", *pipeName, "name", *name, "port", *port,
		"fingerprint", identity.Fingerprint)

	sig := make(chan os.Signal, 1)
	signal.Notify(sig, os.Interrupt, syscall.SIGTERM)
	<-sig

	slog.Info("fgcore shutting down")
	srv.Stop()
	// 先停 Engine（关闭所有会话、不再有入站 HID 帧），再停 Hub 释放注入状态：
	// 反过来会在 ReleaseAll 之后仍可能收到一帧 KEY_DOWN，键就卡在按下状态。
	engine.Stop()
	hub.Stop()
	c.Stop()
	mdnsLink.Stop()
}

func defaultAppData() string {
	if appdata := os.Getenv("APPDATA"); appdata != "" {
		return appdata
	}
	return os.TempDir()
}

// resolveVia 把发现表接给连接引擎。
func resolveVia(c *core.Core) link.Resolver {
	return func(instance string) (net.IP, int, bool) {
		for _, p := range c.Peers() {
			if p.Instance == instance {
				return p.Addr, int(p.Port), true
			}
		}
		return nil, 0, false
	}
}

// startAdvertiser 起本机 mDNS 通告（§3.2.1）。encryption 用扩展值 tls13
// 声明 DirectLink TCP/TLS（规范只枚举了 dtls13/none，见 PROTOCOL-ISSUES P2）。
func startAdvertiser(name string, port int) (*protocol.MdnsAdvertisor, error) {
	ip := outboundIPv4()
	host := sanitizeHost(name)
	txt := protocol.MdnsTXTRecord{
		Version:    "1",
		Platform:   "windows",
		Name:       name,
		Caps:       "intelligentlink,directlink,clipboard,filetransfer",
		Port:       strconv.Itoa(port),
		Encryption: "tls13",
	}
	return protocol.NewMdnsAdvertisor(
		link.InstanceOf(name), host+".local.", ip, uint16(port), txt)
}

// outboundIPv4 取对外默认路由的 IPv4（UDP Dial 不发包，仅查本地路由）。
func outboundIPv4() net.IP {
	conn, err := net.Dial("udp", "224.0.0.251:5353")
	if err != nil {
		return net.IPv4(127, 0, 0, 1)
	}
	defer conn.Close()
	if addr, ok := conn.LocalAddr().(*net.UDPAddr); ok && addr.IP.To4() != nil {
		return addr.IP
	}
	return net.IPv4(127, 0, 0, 1)
}

// sanitizeHost 把设备名净化成合法的 mDNS host 标签（小写字母/数字/-）。
func sanitizeHost(name string) string {
	var b strings.Builder
	for _, r := range strings.ToLower(strings.TrimSpace(name)) {
		switch {
		case r >= 'a' && r <= 'z', r >= '0' && r <= '9', r == '-':
			b.WriteRune(r)
		default:
			b.WriteRune('-')
		}
	}
	if b.Len() == 0 {
		return "flowgrid"
	}
	return b.String()
}

// importPeerCert 把对端证书（PEM）导入信任库：带外交换的首次配对引导
// （PROTOCOL-ISSUES P3 的临时解——规范层解决前，证书经用户手动分发）。
func importPeerCert(trust *link.TrustStore, path, peerName string) error {
	if peerName == "" {
		return errors.New("-peer-cert requires -peer-name")
	}
	pemData, err := os.ReadFile(path)
	if err != nil {
		return err
	}
	block, _ := pem.Decode(pemData)
	if block == nil || block.Type != "CERTIFICATE" {
		return errors.New("no CERTIFICATE block in " + path)
	}
	if _, err := x509.ParseCertificate(block.Bytes); err != nil {
		return err
	}
	return trust.Add(link.Fingerprint(block.Bytes), peerName, block.Bytes)
}

func defaultName() string {
	host, err := os.Hostname()
	if err != nil || host == "" {
		return "Windows PC"
	}
	return host
}

func defaultDataDir() string {
	if appdata := os.Getenv("APPDATA"); appdata != "" {
		return filepath.Join(appdata, "FlowGrid", "core")
	}
	return filepath.Join(os.TempDir(), "flowgrid-core")
}
