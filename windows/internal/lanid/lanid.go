// Package lanid implements the IntelligentLink LAN private-chain discovery
// adapter for Windows fgcore (PROTOCOL.md §3.1). Pure UDP multicast I/O
// feeding the protocol layer's Announcer/Listener — all protocol logic
// (signing, verification, trust chain, link selection) lives in the
// protocol package, shared identically across all three platforms.
package lanid

import (
	"crypto/ecdsa"
	"fmt"
	"net"
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

	mu       sync.Mutex
	sendConn *net.UDPConn
	recvConn *net.UDPConn
	done     chan struct{}
	wg       sync.WaitGroup
	closed   bool

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

	l.wg.Add(2)
	go l.announceLoop(sendConn)
	go l.recvLoop(recvConn)
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
	for {
		n, from, err := conn.ReadFromUDP(buf)
		if err != nil {
			select {
			case <-l.done:
				return
			default:
				return
			}
		}
		l.listener.Feed(buf[:n], from.IP)
		l.notifyPeers()
	}
}

func (l *Link) notifyPeers() {
	if l.OnPeersChanged != nil {
		l.OnPeersChanged(l.listener.Peers())
	}
}

// Prune removes peers not seen within the max age.
func (l *Link) Prune() int {
	return l.listener.Prune(protocol.LanIDPeerMaxAge)
}

// Peers returns the current peer list.
func (l *Link) Peers() []protocol.LanIDPeer {
	return l.listener.Peers()
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
