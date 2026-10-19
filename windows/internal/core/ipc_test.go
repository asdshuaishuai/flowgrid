package core

import (
	"bufio"
	"encoding/json"
	"fmt"
	"net"
	"os"
	"sync/atomic"
	"testing"
	"time"

	"github.com/Microsoft/go-winio"
	"github.com/flowgrid/protocol"
)

var pipeSeq atomic.Int64

func testPipeName() string {
	return fmt.Sprintf(`\\.\pipe\fgtest-%d-%d`, os.Getpid(), pipeSeq.Add(1))
}

// fakeFeed: test-only discovery feed, items are injected by each case.
type fakeFeed struct{ items []protocol.DiscoveredPeer }

func (f *fakeFeed) poll() []protocol.DiscoveredPeer {
	out := f.items
	f.items = nil
	return out
}

func (f *fakeFeed) prune(time.Duration) int { return 0 }

func newTestCore() *Core {
	c := New()
	c.feed = &fakeFeed{}
	return c
}

func mkPeer(name, instance, ip string, port uint16) protocol.DiscoveredPeer {
	return protocol.DiscoveredPeer{
		Name:     name,
		Instance: instance,
		Addr:     net.ParseIP(ip),
		Port:     port,
		Platform: "linux",
		// Explicit timestamps: backdate tests construct expiry deterministically,
		// never relying on the coarse Windows monotonic clock.
		LastSeen: time.Now(),
	}
}

func serve(t *testing.T, c *Core) (name string, srv *Server) {
	t.Helper()
	name = testPipeName()
	ln, err := ListenPipe(name)
	if err != nil {
		t.Fatalf("listen pipe: %v", err)
	}
	srv = Serve(ln, c, nil, nil)
	t.Cleanup(srv.Stop)
	return name, srv
}

type client struct {
	nc  net.Conn
	r   *bufio.Reader
	enc *json.Encoder
}

func dial(t *testing.T, name string) *client {
	t.Helper()
	timeout := 3 * time.Second
	nc, err := winio.DialPipe(name, &timeout)
	if err != nil {
		t.Fatalf("dial pipe: %v", err)
	}
	t.Cleanup(func() { nc.Close() })
	return &client{nc: nc, r: bufio.NewReader(nc), enc: json.NewEncoder(nc)}
}

func (cl *client) send(t *testing.T, env envelope) {
	t.Helper()
	if err := cl.enc.Encode(env); err != nil {
		t.Fatalf("send: %v", err)
	}
}

func (cl *client) recv(t *testing.T) envelope {
	t.Helper()
	line, err := cl.r.ReadString('\n')
	if err != nil {
		t.Fatalf("recv: %v", err)
	}
	var env envelope
	if err := json.Unmarshal([]byte(line), &env); err != nil {
		t.Fatalf("recv not json: %v (%s)", err, line)
	}
	return env
}

func TestIpcHelloDevicesStatus(t *testing.T) {
	c := newTestCore()
	peer := mkPeer("MacBook Pro", "MacBook Pro._flowgrid._tcp.local.", "10.0.0.7", 24801)
	c.peers[peer.Key()] = peer
	name, _ := serve(t, c)
	cl := dial(t, name)

	cl.send(t, envelope{V: 1, Type: "hello", ID: "h1"})
	env := cl.recv(t)
	if env.Type != "hello.ok" || env.ID != "h1" {
		t.Fatalf("hello reply wrong: %+v", env)
	}
	var hello struct {
		Name    string `json:"name"`
		Version string `json:"version"`
	}
	if err := json.Unmarshal(env.Payload, &hello); err != nil || hello.Name != "fgcore" || hello.Version != Version {
		t.Fatalf("hello.ok payload wrong: %s (%v)", env.Payload, err)
	}

	cl.send(t, envelope{V: 1, Type: "devices.list", ID: "d1"})
	env = cl.recv(t)
	if env.Type != "devices" || env.ID != "d1" {
		t.Fatalf("devices reply wrong: %+v", env)
	}
	var devs struct {
		Peers []peerDTO `json:"peers"`
	}
	if err := json.Unmarshal(env.Payload, &devs); err != nil {
		t.Fatalf("devices payload not json: %v", err)
	}
	if len(devs.Peers) != 1 {
		t.Fatalf("want 1 device, got %d", len(devs.Peers))
	}
	p := devs.Peers[0]
	if p.Name != "MacBook Pro" || p.Addr != "10.0.0.7" || p.Port != 24801 {
		t.Fatalf("device dto wrong: %+v", p)
	}

	cl.send(t, envelope{V: 1, Type: "status.get", ID: "s1"})
	env = cl.recv(t)
	if env.Type != "status" || env.ID != "s1" {
		t.Fatalf("status reply wrong: %+v", env)
	}
	var st Status
	if err := json.Unmarshal(env.Payload, &st); err != nil {
		t.Fatalf("status payload not json: %v", err)
	}
	if !st.MDNS || st.Peers != 1 || st.Version != Version {
		t.Fatalf("status wrong: %+v", st)
	}
}

func TestIpcBroadcastOnChange(t *testing.T) {
	c := newTestCore()
	name, _ := serve(t, c)
	cl := dial(t, name)

	// A connected client is not necessarily registered in the broadcast list
	// yet (winio async accept). Round-trip one hello to synchronize; the real
	// link always helloes first anyway.
	cl.send(t, envelope{V: 1, Type: "hello", ID: "sync"})
	if env := cl.recv(t); env.Type != "hello.ok" {
		t.Fatalf("hello sync round-trip failed: %+v", env)
	}

	feed := c.feed.(*fakeFeed)
	feed.items = []protocol.DiscoveredPeer{
		mkPeer("deepin", "deepin._flowgrid._tcp.local.", "192.168.1.30", 24801),
	}
	c.drain()

	env := cl.recv(t)
	if env.Type != "device.changed" || env.ID != "" {
		t.Fatalf("want id-less device.changed push: %+v", env)
	}
	var devs struct {
		Peers []peerDTO `json:"peers"`
	}
	if err := json.Unmarshal(env.Payload, &devs); err != nil || len(devs.Peers) != 1 || devs.Peers[0].Name != "deepin" {
		t.Fatalf("change snapshot wrong: %s (%v)", env.Payload, err)
	}
}

func TestIpcRejectsUnknownTypeAndVersion(t *testing.T) {
	c := newTestCore()
	name, _ := serve(t, c)
	cl := dial(t, name)

	cl.send(t, envelope{V: 1, Type: "nope", ID: "u1"})
	env := cl.recv(t)
	if env.Type != "error" || env.ID != "u1" {
		t.Fatalf("unknown type should error with id: %+v", env)
	}

	cl.send(t, envelope{V: 99, Type: "hello", ID: "v1"})
	env = cl.recv(t)
	if env.Type != "error" || env.ID != "v1" {
		t.Fatalf("bad envelope version should error: %+v", env)
	}

	// A bad line must not kill the connection.
	if _, err := cl.nc.Write([]byte("this is not json\n")); err != nil {
		t.Fatalf("write: %v", err)
	}
	if env := cl.recv(t); env.Type != "error" {
		t.Fatalf("bad line should error: %+v", env)
	}
	cl.send(t, envelope{V: 1, Type: "hello", ID: "h2"})
	if env := cl.recv(t); env.Type != "hello.ok" || env.ID != "h2" {
		t.Fatalf("connection must survive bad lines: %+v", env)
	}
}

func TestIpcSelfInfoAndLinkMessages(t *testing.T) {
	c := newTestCore()
	name, _ := serve(t, c)
	cl := dial(t, name)

	// Without a link engine, link-class messages must error, not panic.
	cl.send(t, envelope{V: 1, Type: "self.info", ID: "s1"})
	if env := cl.recv(t); env.Type != "error" || env.ID != "s1" {
		t.Fatalf("self.info without engine should error: %+v", env)
	}
	cl.send(t, envelope{V: 1, Type: "peers.list", ID: "p1"})
	if env := cl.recv(t); env.Type != "error" || env.ID != "p1" {
		t.Fatalf("peers.list without engine should error: %+v", env)
	}
	cl.send(t, envelope{V: 1, Type: "connect.request", ID: "c1", Payload: mustJSON(map[string]string{"instance": "x._flowgrid._tcp.local."})})
	if env := cl.recv(t); env.Type != "error" || env.ID != "c1" {
		t.Fatalf("connect.request without engine should error: %+v", env)
	}
}

func TestCoreSweepPrunesAndNotifies(t *testing.T) {
	c := newTestCore()
	seen := make(chan struct{}, 1)
	c.OnChange(func() { seen <- struct{}{} })

	old := mkPeer("stale", "stale._flowgrid._tcp.local.", "10.0.0.9", 24801)
	old.LastSeen = time.Now().Add(-2 * PruneAge) // backdate: deterministic expiry
	fresh := mkPeer("fresh", "fresh._flowgrid._tcp.local.", "10.0.0.8", 24801)
	c.peers[old.Key()] = old
	c.peers[fresh.Key()] = fresh

	c.sweep()

	select {
	case <-seen:
	default:
		t.Fatal("pruning must notify watchers")
	}
	if got := c.Peers(); len(got) != 1 || got[0].Name != "fresh" {
		t.Fatalf("stale peer should be pruned, left: %v", got)
	}
}

func TestCoreSkipsSelfAdvertisement(t *testing.T) {
	c := newTestCore()
	c.SetSelfInstance("self._flowgrid._tcp.local.")

	feed := c.feed.(*fakeFeed)
	feed.items = []protocol.DiscoveredPeer{
		mkPeer("self", "self._flowgrid._tcp.local.", "10.0.0.1", 24801),
		mkPeer("other", "other._flowgrid._tcp.local.", "10.0.0.2", 24801),
	}
	c.drain()

	if got := c.Peers(); len(got) != 1 || got[0].Name != "other" {
		t.Fatalf("self advertisement must be filtered, left: %v", got)
	}
}

func TestCoreDegradedWithoutMDNS(t *testing.T) {
	c := New() // no feed: mdns unavailable
	c.drain()  // must not panic
	if st := c.Status(); st.MDNS || st.Peers != 0 {
		t.Fatalf("degraded status must report honestly: %+v", st)
	}
}
