// fgcore 的设备侧：发现 feed 桥接、设备表与变更广播。
//
// 目标三层形态（SYSTEM 服务 fgcored → 会话核心 fgcore → WPF UI）见
// openspec/changes/windows-client/design.md；本包当前承载发现与设备表，
// 输入注入与对端连接后续并入。IPC 契约见 specs/windows-ipc。
package core

import (
	"net"
	"sort"
	"sync"
	"time"

	"github.com/flowgrid/protocol"
)

// Version 是 fgcore 的语义化版本，hello.ok 与 status 如实上报。
const Version = "0.1.0"

// PruneAge 内未再通告的设备视为离线。与 mDNS 记录 TTL(120s) 对齐取其一半，
// 保证 UI 侧的离线感知不晚于对端真正失效。
const PruneAge = time.Minute

// discoveryFeed 抽出对 MdnsBrowser 的依赖：单测注入假 feed，不碰真实组播。
type discoveryFeed interface {
	poll() []protocol.DiscoveredPeer
	prune(maxAge time.Duration) int
}

type mdnsFeed struct{ b *protocol.MdnsBrowser }

func (f mdnsFeed) poll() []protocol.DiscoveredPeer { return f.b.Poll() }
func (f mdnsFeed) prune(maxAge time.Duration) int  { return f.b.Prune(maxAge) }

// Core 持有设备表并向 IPC 层提供快照与变更通知。
type Core struct {
	mu           sync.Mutex
	peers        map[string]protocol.DiscoveredPeer
	feed         discoveryFeed
	browser      *protocol.MdnsBrowser
	mdnsErr      error
	started      time.Time
	watch        map[int]func()
	nextID       int
	selfInstance string // 本机实例名：自通告会被自己的浏览器收到，须过滤

	stop chan struct{}
	wg   sync.WaitGroup
}

func New() *Core {
	return &Core{
		peers:   map[string]protocol.DiscoveredPeer{},
		started: time.Now(),
		watch:   map[int]func(){},
		stop:    make(chan struct{}),
	}
}

// Start 尝试起 mDNS 浏览并进入发现/清扫循环。组播不可用不是致命错误：
// 照常降级服务，原因记入 Status 由 UI 如实展示。
func (c *Core) Start() {
	b := protocol.NewMdnsBrowser()
	c.mu.Lock()
	c.browser = b
	c.feed = mdnsFeed{b}
	c.mu.Unlock()

	c.wg.Add(1)
	go c.run()
}

// Browser returns the mDNS browser for the socket manager to drive.
func (c *Core) Browser() *protocol.MdnsBrowser {
	c.mu.Lock()
	defer c.mu.Unlock()
	return c.browser
}

// SetMdnsErr records an mDNS socket error for Status reporting.
func (c *Core) SetMdnsErr(err error) {
	c.mu.Lock()
	c.mdnsErr = err
	c.mu.Unlock()
}

func (c *Core) run() {
	defer c.wg.Done()
	poll := time.NewTicker(500 * time.Millisecond)
	defer poll.Stop()
	sweep := time.NewTicker(30 * time.Second)
	defer sweep.Stop()
	for {
		select {
		case <-c.stop:
			return
		case <-poll.C:
			c.drain()
		case <-sweep.C:
			c.sweep()
		}
	}
}

// SetSelfInstance 设置本机实例名，drain 时过滤自身通告。
func (c *Core) SetSelfInstance(instance string) {
	c.mu.Lock()
	c.selfInstance = instance
	c.mu.Unlock()
}

// AddManualPeer 注入一台手工设备。
//
// 两个用途：同机双进程冒烟（Windows 上 mDNS 单播应答会被同端口的另一个
// 套接字截走，环回发现不可靠——见 windows-connection design.md），以及将来
// 「手动添加已知设备」的产品功能面。地址变更可直接重复调用刷新。
func (c *Core) AddManualPeer(name, ip string, port uint16) {
	p := protocol.DiscoveredPeer{
		Name:     name,
		Instance: name + "._flowgrid._tcp.local.",
		Addr:     net.ParseIP(ip),
		Port:     port,
		Platform: "manual",
		LastSeen: time.Now(),
	}
	c.mu.Lock()
	if c.selfInstance == "" || p.Instance != c.selfInstance {
		c.peers[p.Key()] = p
	}
	c.mu.Unlock()
	c.notify()
}

// drain 收走本轮新发现/更新的设备；有变化才广播。
func (c *Core) drain() {
	c.mu.Lock()
	feed := c.feed
	self := c.selfInstance
	c.mu.Unlock()
	if feed == nil {
		return
	}
	got := feed.poll()
	if len(got) == 0 {
		return
	}
	c.mu.Lock()
	for _, p := range got {
		if self != "" && p.Instance == self {
			continue // 自己的通告
		}
		c.peers[p.Key()] = p
	}
	c.mu.Unlock()
	c.notify()
}

// sweep 淘汰自家设备表里超龄的设备，并同步瘦 feed（browser）侧的表——
// 两边规则一致（time.Since(LastSeen) > PruneAge）， 只是入表时机差一次轮询。
func (c *Core) sweep() {
	c.mu.Lock()
	feed := c.feed
	n := 0
	for k, p := range c.peers {
		if time.Since(p.LastSeen) > PruneAge {
			delete(c.peers, k)
			n++
		}
	}
	c.mu.Unlock()
	if feed != nil {
		feed.prune(PruneAge)
	}
	if n > 0 {
		c.notify()
	}
}

func (c *Core) Stop() {
	close(c.stop)
	c.wg.Wait()
	// mDNS socket 由 mdnslink 持有并负责关闭；浏览器本身是纯逻辑，
	// 这里不再（也不该）替它停 socket。
}

// Status 是 status.get 的载荷，字段命名即 windows-ipc 契约。
type Status struct {
	MDNS      bool   `json:"mdns"`
	MDNSErr   string `json:"mdns_error,omitempty"`
	Peers     int    `json:"peers"`
	UptimeSec int64  `json:"uptime_s"`
	Version   string `json:"version"`
}

func (c *Core) Status() Status {
	c.mu.Lock()
	defer c.mu.Unlock()
	s := Status{
		Peers:     len(c.peers),
		UptimeSec: int64(time.Since(c.started).Seconds()),
		Version:   Version,
	}
	// MDNS 表示「socket 真的在跑」：feed 存在但 socket 起失败时是 false，
	// 具体原因走 MDNSErr。别把「有浏览器对象」当成「发现可用」。
	s.MDNS = c.feed != nil && c.mdnsErr == nil
	if c.mdnsErr != nil {
		s.MDNSErr = c.mdnsErr.Error()
	}
	return s
}

// Peers 返回设备表快照，按名称（其次 key）稳定排序，保证 UI 多次刷新顺序一致。
func (c *Core) Peers() []protocol.DiscoveredPeer {
	c.mu.Lock()
	defer c.mu.Unlock()
	out := make([]protocol.DiscoveredPeer, 0, len(c.peers))
	for _, p := range c.peers {
		out = append(out, p)
	}
	sort.Slice(out, func(i, j int) bool {
		if out[i].Name != out[j].Name {
			return out[i].Name < out[j].Name
		}
		return out[i].Key() < out[j].Key()
	})
	return out
}

// OnChange 订阅设备表变更（发现更新与过期淘汰都算），返回取消订阅函数。
// 回调在核心的循环 goroutine 上触发，订阅方不得在回调里做重活。
func (c *Core) OnChange(fn func()) (cancel func()) {
	c.mu.Lock()
	defer c.mu.Unlock()
	id := c.nextID
	c.nextID++
	c.watch[id] = fn
	return func() {
		c.mu.Lock()
		defer c.mu.Unlock()
		delete(c.watch, id)
	}
}

func (c *Core) notify() {
	c.mu.Lock()
	fns := make([]func(), 0, len(c.watch))
	for _, f := range c.watch {
		fns = append(fns, f)
	}
	c.mu.Unlock()
	for _, f := range fns {
		f()
	}
}
