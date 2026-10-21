// Package lanid implements the IntelligentLink LAN private-chain discovery
// adapter for Windows fgcore (PROTOCOL.md §3.1). Pure UDP multicast I/O
// feeding the protocol layer's Announcer/Listener — all protocol logic
// (signing, verification, trust chain, link selection) lives in the
// protocol package, shared identically across all three platforms.
package lanid

import (
	"bytes"
	"crypto/ecdsa"
	"fmt"
	"net"
	"strings"
	"sync"
	"time"

	"github.com/flowgrid/protocol"
)

// Link is the UDP multicast adapter for LAN private-chain discovery.
// Mirrors the mac client's LanIDLink.swift: the client owns the UDP socket,
// the protocol layer owns the logic.
type Link struct {
	announcer *protocol.LanIDAnnouncer
	listener  *protocol.LanIDListener

	// selfKey 是本机身份公钥：多播会回环到自己，必须从对端表里滤掉，
	// 否则 UI 会把自己列成一台设备并可发起自连。
	selfKey []byte

	mu       sync.Mutex
	sendConn *net.UDPConn
	recvConn *net.UDPConn
	done     chan struct{}
	wg       sync.WaitGroup
	closed   bool

	// notifyMu 保护 lastPeers：通告每 2s 一次，只有可见内容变化才回调，
	// 否则 UI 会持续被无意义刷新。
	notifyMu  sync.Mutex
	lastPeers string

	// OnPeersChanged is called when the peer list changes.
	OnPeersChanged func([]protocol.LanIDPeer)
}

// NewLink creates a LAN identification link with the given identity key.
func NewLink(identity *ecdsa.PrivateKey, platform protocol.Platform, caps protocol.CapabilityFlags, tcpPort uint16, name string) (*Link, error) {
	announcer, err := protocol.NewLanIDAnnouncer(identity, platform, caps, tcpPort, name)
	if err != nil {
		return nil, fmt.Errorf("lanid: new announcer: %w", err)
	}
	return &Link{
		announcer: announcer,
		listener:  protocol.NewLanIDListener(),
		selfKey:   protocol.IdentityPublicKeyBytes(identity),
		done:      make(chan struct{}),
	}, nil
}

// Start begins the multicast send/receive loops.
func (l *Link) Start() error {
	l.mu.Lock()
	if l.closed {
		l.mu.Unlock()
		return fmt.Errorf("lanid: link closed")
	}
	if l.sendConn != nil {
		l.mu.Unlock()
		return nil
	}
	l.mu.Unlock()

	gaddr, err := net.ResolveUDPAddr("udp", fmt.Sprintf("%s:%d", protocol.LanIDGroup, protocol.LanIDPort))
	if err != nil {
		return fmt.Errorf("lanid: resolve group: %w", err)
	}
	sendConn, err := net.DialUDP("udp", nil, gaddr)
	if err != nil {
		return fmt.Errorf("lanid: dial: %w", err)
	}

	recvAddr, err := net.ResolveUDPAddr("udp", fmt.Sprintf(":%d", protocol.LanIDPort))
	if err != nil {
		sendConn.Close()
		return fmt.Errorf("lanid: resolve recv: %w", err)
	}
	recvConn, err := net.ListenMulticastUDP("udp", nil, recvAddr)
	if err != nil {
		sendConn.Close()
		return fmt.Errorf("lanid: listen multicast: %w", err)
	}

	l.mu.Lock()
	if l.closed {
		l.mu.Unlock()
		sendConn.Close()
		recvConn.Close()
		return fmt.Errorf("lanid: link closed")
	}
	l.sendConn = sendConn
	l.recvConn = recvConn
	l.mu.Unlock()

	l.wg.Add(3)
	go l.announceLoop(sendConn)
	go l.recvLoop(recvConn)
	go l.pruneLoop()
	return nil
}

func (l *Link) announceLoop(conn *net.UDPConn) {
	defer l.wg.Done()
	ticker := time.NewTicker(protocol.LanIDAnnouncePeriod)
	defer ticker.Stop()
	l.announceOnce(conn)
	for {
		select {
		case <-l.done:
			return
		case <-ticker.C:
			l.announceOnce(conn)
		}
	}
}

func (l *Link) announceOnce(conn *net.UDPConn) {
	wire, err := l.announcer.Announce()
	if err != nil {
		return
	}
	_, _ = conn.Write(wire)
}

func (l *Link) recvLoop(conn *net.UDPConn) {
	defer l.wg.Done()
	buf := make([]byte, 65536)
	// 与 mdnslink 一致：UDP 读错误（Windows 的 WSAECONNRESET 尤其常见）不能
	// 自旋，也不能让发现线程直接死掉。
	backoff := 50 * time.Millisecond
	for {
		n, from, err := conn.ReadFromUDP(buf)
		if err != nil {
			select {
			case <-l.done:
				return
			default:
			}
			time.Sleep(backoff)
			if backoff < time.Second {
				backoff *= 2
			}
			continue
		}
		backoff = 50 * time.Millisecond
		l.listener.Feed(buf[:n], from.IP)
		l.notifyPeers()
	}
}

func (l *Link) pruneLoop() {
	defer l.wg.Done()
	ticker := time.NewTicker(protocol.LanIDPeerMaxAge)
	defer ticker.Stop()
	for {
		select {
		case <-l.done:
			return
		case <-ticker.C:
			if n := l.listener.Prune(protocol.LanIDPeerMaxAge); n > 0 {
				l.notifyPeers()
			}
		}
	}
}

// peers 返回过滤掉本机后的对端快照。
func (l *Link) peers() []protocol.LanIDPeer {
	all := l.listener.Peers()
	out := make([]protocol.LanIDPeer, 0, len(all))
	for _, p := range all {
		if bytes.Equal(p.PubKey, l.selfKey) {
			continue
		}
		out = append(out, p)
	}
	return out
}

// notifyPeers 只在可见内容变化时回调（引擎侧每 2s 都会收到同一条通告）。
func (l *Link) notifyPeers() {
	fn := l.OnPeersChanged
	if fn == nil {
		return
	}
	peers := l.peers()
	var sig strings.Builder
	for _, p := range peers {
		fmt.Fprintf(&sig, "%s|%s|%s|%d;", p.Fingerprint, p.Name, p.Addr, p.Port)
	}
	key := sig.String()

	l.notifyMu.Lock()
	if key == l.lastPeers {
		l.notifyMu.Unlock()
		return
	}
	l.lastPeers = key
	l.notifyMu.Unlock()

	fn(append([]protocol.LanIDPeer(nil), peers...))
}

// Prune removes peers not seen within the max age.
func (l *Link) Prune() int {
	return l.listener.Prune(protocol.LanIDPeerMaxAge)
}

// Peers returns the current peer list (self excluded).
func (l *Link) Peers() []protocol.LanIDPeer {
	return l.peers()
}

// Stop stops the multicast loops and closes sockets.
func (l *Link) Stop() {
	l.mu.Lock()
	if l.closed {
		l.mu.Unlock()
		return
	}
	l.closed = true
	sendConn := l.sendConn
	recvConn := l.recvConn
	l.sendConn = nil
	l.recvConn = nil
	l.mu.Unlock()
	close(l.done)
	if sendConn != nil {
		sendConn.Close()
	}
	if recvConn != nil {
		recvConn.Close()
	}
	l.wg.Wait()
}
