// Package mdnslink implements the mDNS UDP socket manager for Windows fgcore.
// It owns the UDP socket and drives the protocol layer's MdnsBrowser/MdnsAdvertisor.
// Pure I/O: all protocol logic (DNS parsing, peer aggregation, query/response
// building) lives in the protocol package — this file only does socket I/O,
// mirroring the mac client's LanIDLink.swift and the deepin client's
// lanidlink.cpp.
package mdnslink

import (
	"fmt"
	"net"
	"sync"
	"time"

	"github.com/flowgrid/protocol"
)

const queryPeriod = 2 * time.Second

// Link owns the mDNS UDP socket and drives the browser/advertiser.
type Link struct {
	browser    *protocol.MdnsBrowser
	advertiser *protocol.MdnsAdvertisor

	mu     sync.Mutex
	conn   *net.UDPConn
	done   chan struct{}
	wg     sync.WaitGroup
	closed bool
}

// NewLink creates an mDNS socket manager with the given browser and advertiser.
func NewLink(browser *protocol.MdnsBrowser, advertiser *protocol.MdnsAdvertisor) *Link {
	return &Link{
		browser:    browser,
		advertiser: advertiser,
		done:       make(chan struct{}),
	}
}

// Start binds the UDP socket and starts the query/receive loops.
func (l *Link) Start() error {
	l.mu.Lock()
	if l.closed {
		l.mu.Unlock()
		return fmt.Errorf("mdnslink: closed")
	}
	if l.conn != nil {
		l.mu.Unlock()
		return nil
	}
	l.mu.Unlock()

	addr := &net.UDPAddr{IP: net.ParseIP(protocol.MdnsGroup), Port: protocol.MdnsPort}
	conn, err := net.ListenMulticastUDP("udp4", nil, addr)
	if err != nil {
		return fmt.Errorf("mdnslink: listen %s: %w", addr, err)
	}

	l.mu.Lock()
	if l.closed {
		l.mu.Unlock()
		conn.Close()
		return fmt.Errorf("mdnslink: closed")
	}
	l.conn = conn
	l.mu.Unlock()

	l.wg.Add(2)
	go l.queryLoop(conn)
	go l.recvLoop(conn)
	return nil
}

func (l *Link) queryLoop(conn *net.UDPConn) {
	defer l.wg.Done()
	ticker := time.NewTicker(queryPeriod)
	defer ticker.Stop()
	l.sendQuery(conn)
	for {
		select {
		case <-l.done:
			return
		case <-ticker.C:
			l.sendQuery(conn)
		}
	}
}

func (l *Link) sendQuery(conn *net.UDPConn) {
	msg := protocol.BuildQueryPacket()
	dst := &net.UDPAddr{IP: net.ParseIP(protocol.MdnsGroup), Port: protocol.MdnsPort}
	_, _ = conn.WriteToUDP(msg, dst)
}

func (l *Link) recvLoop(conn *net.UDPConn) {
	defer l.wg.Done()
	buf := make([]byte, protocol.MdnsMaxPacket)
	// Windows 上 UDP 套接字会因先前发出的包收到 ICMP 端口不可达而持续返回
	// WSAECONNRESET。原样 continue 会变成满 CPU 空转；这里退避重试，不退出
	// （组播可用性可能只是暂时变差）。
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
		l.handlePacket(conn, buf[:n], from)
	}
}

func (l *Link) handlePacket(conn *net.UDPConn, data []byte, from *net.UDPAddr) {
	// 查询：advertiser 应答
	if l.advertiser != nil {
		if resp := l.advertiser.HandleQuery(data); resp != nil {
			dst := &net.UDPAddr{IP: net.ParseIP(protocol.MdnsGroup), Port: protocol.MdnsPort}
			_, _ = conn.WriteToUDP(resp, dst)
			return
		}
	}
	// 应答：browser 聚合
	if l.browser != nil {
		l.browser.Feed(data, from.IP)
	}
}

// Stop stops the loops and closes the socket.
func (l *Link) Stop() {
	l.mu.Lock()
	if l.closed {
		l.mu.Unlock()
		return
	}
	l.closed = true
	conn := l.conn
	l.conn = nil
	l.mu.Unlock()
	close(l.done)
	if conn != nil {
		conn.Close()
	}
	l.wg.Wait()
}
