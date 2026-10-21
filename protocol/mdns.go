// DirectLink 的 mDNS 发现（PROTOCOL.md §3.2）。
//
// 这里自己实现 DNS 报文编解码，而不是引入 zeroconf 之类的依赖：协议里对
// TXT 字段（TXTRecord）和服务名（_flowgrid._tcp）都有明确约定，而
// discovery.go 已经有 MdnsTXTRecord 的编解码，接上去就是完整闭环。
//
// 解析必须支持 DNS 名字压缩指针（0xC0 偏移），否则真实 responder 回来的报文
// 一律解析失败——mDNS 几乎每条记录都用压缩。
package protocol

import (
	"encoding/binary"
	"errors"
	"net"
	"strings"
	"sync"
	"time"
)

// DNS 资源记录类型（只用得上的几个）。
const (
	dnsTypeA    uint16 = 1
	dnsTypePTR  uint16 = 12
	dnsTypeTXT  uint16 = 16
	dnsTypeAAAA uint16 = 28
	dnsTypeSRV  uint16 = 33
)

const (
	dnsClassIN        uint16 = 1
	dnsClassCacheFlsh uint16 = 0x8000
	dnsFlagResponse   uint16 = 0x8000
	dnsFlagQu         uint16 = 0x0000
)

const (
	MdnsGroup     = "224.0.0.251"
	MdnsPort      = 5353
	mdnsService   = MdnsServiceType + ".local."
	MdnsMaxPacket = 9000
)

var errMalformedDNS = errors.New("protocol: malformed mDNS message")

type dnsHeader struct {
	id                            uint16
	flags                         uint16
	questions, answers, authority uint16
	additional                    uint16
}

type dnsQuestion struct {
	name  string
	qtype uint16
}

type dnsRecord struct {
	name  string
	rtype uint16
	class uint16
	ttl   uint32

	ptrTarget string
	srvHost   string
	srvPort   uint16
	txt       []string
	addr      net.IP
}

// parseName 读一个 DNS 名字，支持 0xC0 压缩指针。limit 是可回跳的最大字节数，
// 用来防压缩指针成环导致的死循环。
func parseName(buf []byte, off int, depth int) (string, int, error) {
	if depth > 8 {
		return "", 0, errMalformedDNS
	}
	var sb strings.Builder
	for {
		if off >= len(buf) {
			return "", 0, errMalformedDNS
		}
		l := int(buf[off])
		switch {
		case l == 0:
			return sb.String(), off + 1, nil
		case l&0xC0 == 0xC0:
			if off+1 >= len(buf) {
				return "", 0, errMalformedDNS
			}
			ptr := int(binary.BigEndian.Uint16(buf[off:off+2]) & 0x3FFF)
			// 必须从 buf[0] 起跳，且 off 必须前进，否则解析器不终止。
			sub, _, err := parseName(buf, ptr, depth+1)
			if err != nil {
				return "", 0, err
			}
			sb.WriteString(sub)
			return sb.String(), off + 2, nil
		case l&0xC0 != 0:
			return "", 0, errMalformedDNS
		default:
			if off+1+l > len(buf) {
				return "", 0, errMalformedDNS
			}
			sb.WriteString(string(buf[off+1 : off+1+l]))
			sb.WriteByte('.')
			off += 1 + l
		}
	}
}

func appendName(dst []byte, name string) []byte {
	name = strings.TrimSuffix(name, ".")
	for _, label := range strings.Split(name, ".") {
		if len(label) > 63 {
			label = label[:63]
		}
		dst = append(dst, byte(len(label)))
		dst = append(dst, label...)
	}
	return append(dst, 0)
}

func (q dnsQuestion) marshal() []byte {
	out := appendName(nil, q.name)
	out = binary.BigEndian.AppendUint16(out, q.typeOrClass())
	return binary.BigEndian.AppendUint16(out, q.classOnly())
}

func (q dnsQuestion) typeOrClass() uint16 { return q.qtype }
func (q dnsQuestion) classOnly() uint16   { return dnsClassIN }

type dnsMessage struct {
	header     dnsHeader
	questions  []dnsQuestion
	answers    []dnsRecord
	additional []dnsRecord
}

func parseDNSMessage(buf []byte) (*dnsMessage, error) {
	if len(buf) < 12 {
		return nil, errMalformedDNS
	}
	m := &dnsMessage{}
	m.header.id = binary.BigEndian.Uint16(buf[0:2])
	m.header.flags = binary.BigEndian.Uint16(buf[2:4])
	m.header.questions = binary.BigEndian.Uint16(buf[4:6])
	m.header.answers = binary.BigEndian.Uint16(buf[6:8])
	m.header.authority = binary.BigEndian.Uint16(buf[8:10])
	m.header.additional = binary.BigEndian.Uint16(buf[10:12])

	off := 12
	for i := 0; i < int(m.header.questions); i++ {
		name, next, err := parseName(buf, off, 0)
		if err != nil {
			return nil, err
		}
		if next+4 > len(buf) {
			return nil, errMalformedDNS
		}
		m.questions = append(m.questions, dnsQuestion{
			name:  name,
			qtype: binary.BigEndian.Uint16(buf[next : next+2]),
		})
		off = next + 4
	}

	total := int(m.header.answers) + int(m.header.additional)
	for i := 0; i < total; i++ {
		rec, next, err := parseRecord(buf, off)
		if err != nil {
			// 尾部截断的记录直接截断解析，不让一条坏记录废掉整包。
			break
		}
		if i < int(m.header.answers) {
			m.answers = append(m.answers, rec)
		} else {
			m.additional = append(m.additional, rec)
		}
		off = next
	}
	return m, nil
}

func parseRecord(buf []byte, off int) (dnsRecord, int, error) {
	var rec dnsRecord
	name, next, err := parseName(buf, off, 0)
	if err != nil {
		return rec, 0, err
	}
	off = next
	if off+10 > len(buf) {
		return rec, 0, errMalformedDNS
	}
	rec.name = name
	rec.rtype = binary.BigEndian.Uint16(buf[off : off+2])
	rec.class = binary.BigEndian.Uint16(buf[off+2 : off+4])
	rec.ttl = binary.BigEndian.Uint32(buf[off+4 : off+8])
	rdlen := int(binary.BigEndian.Uint16(buf[off+8 : off+10]))
	off += 10
	if off+rdlen > len(buf) {
		return rec, 0, errMalformedDNS
	}
	rdata := buf[off : off+rdlen]

	switch rec.rtype {
	case dnsTypePTR:
		target, _, err := parseName(buf, off, 0)
		if err != nil {
			return rec, 0, err
		}
		rec.ptrTarget = target
	case dnsTypeSRV:
		if rdlen < 6 {
			return rec, 0, errMalformedDNS
		}
		rec.srvPort = binary.BigEndian.Uint16(rdata[4:6])
		host, _, err := parseName(buf, off+6, 0)
		if err != nil {
			return rec, 0, err
		}
		rec.srvHost = host
	case dnsTypeTXT:
		for p := 0; p < len(rdata); {
			l := int(rdata[p])
			if p+1+l > len(rdata) {
				break
			}
			rec.txt = append(rec.txt, string(rdata[p+1:p+1+l]))
			p += 1 + l
		}
	case dnsTypeA:
		if rdlen == 4 {
			rec.addr = net.IPv4(rdata[0], rdata[1], rdata[2], rdata[3])
		}
	case dnsTypeAAAA:
		if rdlen == 16 {
			rec.addr = net.IP(append([]byte(nil), rdata...))
		}
	}
	return rec, off + rdlen, nil
}

func (rec dnsRecord) marshal() []byte {
	out := appendName(nil, rec.name)
	out = binary.BigEndian.AppendUint16(out, rec.rtype)
	class := rec.class &^ dnsClassCacheFlsh
	out = binary.BigEndian.AppendUint16(out, class)
	out = binary.BigEndian.AppendUint32(out, rec.ttl)

	var rdata []byte
	switch rec.rtype {
	case dnsTypePTR:
		rdata = appendName(nil, rec.ptrTarget)
	case dnsTypeSRV:
		rdata = append(rdata, 0, 0, 0, 0)
		rdata = binary.BigEndian.AppendUint16(rdata, rec.srvPort)
		rdata = appendName(rdata, rec.srvHost)
	case dnsTypeTXT:
		for _, t := range rec.txt {
			if len(t) > 255 {
				t = t[:255]
			}
			rdata = append(rdata, byte(len(t)))
			rdata = append(rdata, t...)
		}
		if len(rdata) == 0 {
			rdata = []byte{0}
		}
	case dnsTypeA:
		if ip4 := rec.addr.To4(); ip4 != nil {
			rdata = append(rdata, ip4...)
		}
	case dnsTypeAAAA:
		if rec.addr != nil {
			rdata = append(rdata, rec.addr.To16()...)
		}
	}
	out = binary.BigEndian.AppendUint16(out, uint16(len(rdata)))
	return append(out, rdata...)
}

// trimDot 去掉 FQDN 末尾的点，便于做大小写无关的后缀比较。
func trimDot(name string) string { return strings.TrimSuffix(name, ".") }

// srvInfo 是 SRV 记录解析后的内容，用来把 PTR 指向的实例名补成 host:port。
type srvInfo struct {
	host string
	port uint16
}

// ── 发现的设备 ──────────────────────────────────────────────────────────

// DiscoveredPeer 是 mDNS 看到的一台 FlowGrid。
type DiscoveredPeer struct {
	Name       string // 设备名（TXT 的 name）
	Instance   string // 服务实例名，兼作稳定标识
	Addr       net.IP
	Port       uint16
	Platform   string
	Version    string
	Caps       string
	Encryption string
	HWAddr     string
	FirstSeen  time.Time
	LastSeen   time.Time
}

// Key 用于把同一台设备的多次公告合并到一条记录。
func (d DiscoveredPeer) Key() string {
	if len(d.Addr) == 0 {
		return d.Instance
	}
	return d.Instance + "|" + d.Addr.String()
}

// ── 浏览与通告 ──────────────────────────────────────────────────────────

// MdnsBrowser 聚合 mDNS 应答里的对端（纯逻辑：客户端持有 UDP socket，喂字节进来）。
//
// 语义和会话层一致：Go 侧缓存、C 侧轮询。客户端负责：
//   · 绑定 5353 组播、加入 MdnsGroup
//   · 周期发送 BuildQueryPacket() 产生的查询
//   · 收到的应答喂给 Feed()
type MdnsBrowser struct {
	mu      sync.Mutex
	peers   map[string]DiscoveredPeer
	pending []DiscoveredPeer
}

func NewMdnsBrowser() *MdnsBrowser {
	return &MdnsBrowser{peers: map[string]DiscoveredPeer{}}
}

// BuildQueryPacket 产生一条 PTR 查询包（客户端经 UDP 多播发送）。
func BuildQueryPacket() []byte {
	out := make([]byte, 0, 32)
	out = binary.BigEndian.AppendUint16(out, 0) // id
	out = binary.BigEndian.AppendUint16(out, dnsFlagQu)
	out = binary.BigEndian.AppendUint16(out, 1) // qdcount
	out = binary.BigEndian.AppendUint16(out, 0) // ancount
	out = binary.BigEndian.AppendUint16(out, 0) // nscount
	out = binary.BigEndian.AppendUint16(out, 0) // arcount
	out = appendName(out, mdnsService)
	out = binary.BigEndian.AppendUint16(out, dnsTypePTR)
	out = binary.BigEndian.AppendUint16(out, dnsClassIN)
	return out
}

// Feed 喂入一条收到的 mDNS 应答与源 IP；解析成功则聚合，失败静默丢弃。
func (b *MdnsBrowser) Feed(data []byte, from net.IP) {
	msg, err := parseDNSMessage(data)
	if err != nil {
		return
	}
	if msg.header.flags&dnsFlagResponse == 0 {
		return // 查询，不是应答
	}

	// 合并 answer 与 additional：SRV/A 记录常常就跟在 additional 里。
	recs := append(append([]dnsRecord(nil), msg.answers...), msg.additional...)

	// 先把 SRV / A 收集起来，才能给 PTR 补齐地址与端口。
	srvs := map[string]srvInfo{}
	addrs := map[string]net.IP{}
	for _, rec := range recs {
		switch rec.rtype {
		case dnsTypeSRV:
			srvs[rec.name] = srvInfo{host: rec.srvHost, port: rec.srvPort}
		case dnsTypeA, dnsTypeAAAA:
			if rec.addr != nil {
				addrs[rec.name] = rec.addr
			}
		}
	}

	txtByInstance := map[string][]string{}
	for _, rec := range recs {
		if rec.rtype == dnsTypeTXT {
			txtByInstance[rec.name] = rec.txt
		}
	}

	now := time.Now()
	for _, rec := range recs {
		if rec.rtype != dnsTypePTR {
			continue
		}
		instance := rec.ptrTarget
		// 实例名是 "<设备名>._flowgrid._tcp.local."，服务名只是它的后缀。
		if !strings.EqualFold(trimDot(instance), "."+trimDot(mdnsService)) &&
			!strings.HasSuffix(strings.ToLower(trimDot(instance)),
				"."+strings.ToLower(trimDot(mdnsService))) {
			continue
		}
		info, ok := srvs[instance]
		if !ok {
			continue // SRV 还没到，等下一包
		}
		ip := addrs[info.host]
		if ip == nil {
			// mDNS 常见 SRV 先于 A 到达；这时退回发送方地址。
			if from != nil {
				ip = from
			} else {
				continue
			}
		}

		txt := UnmarshalTXT(txtByInstance[instance])
		peer := DiscoveredPeer{
			Instance:   instance,
			Addr:       ip,
			Port:       info.port,
			Platform:   txt.Platform,
			Version:    txt.Version,
			Caps:       txt.Caps,
			Encryption: txt.Encryption,
			HWAddr:     txt.HWAddr,
			LastSeen:   now,
		}
		peer.Name = txt.Name
		if peer.Name == "" {
			peer.Name = strings.SplitN(strings.TrimSuffix(instance, "."), ".", 2)[0]
		}

		b.mu.Lock()
		if old, seen := b.peers[peer.Key()]; seen {
			peer.FirstSeen = old.FirstSeen
		} else {
			peer.FirstSeen = now
		}
		b.peers[peer.Key()] = peer
		b.pending = append(b.pending, peer)
		b.mu.Unlock()
	}
}

// Poll 取走自上次调用以来新发现/更新的设备。取走后即出队，避免重复通知。
func (b *MdnsBrowser) Poll() []DiscoveredPeer {
	b.mu.Lock()
	defer b.mu.Unlock()
	out := b.pending
	b.pending = nil
	return out
}

// Peers 返回当前已知设备的快照（不含已过期的）。
func (b *MdnsBrowser) Peers() []DiscoveredPeer {
	b.mu.Lock()
	defer b.mu.Unlock()
	out := make([]DiscoveredPeer, 0, len(b.peers))
	for _, p := range b.peers {
		out = append(out, p)
	}
	return out
}

// Prune 丢掉超过 maxAge 时间没再出现的设备，返回被淘汰的数量。
func (b *MdnsBrowser) Prune(maxAge time.Duration) int {
	b.mu.Lock()
	defer b.mu.Unlock()
	n := 0
	for k, p := range b.peers {
		if time.Since(p.LastSeen) > maxAge {
			delete(b.peers, k)
			n++
		}
	}
	return n
}

// MdnsAdvertisor 组装本机的 PTR / SRV / TXT / A 记录（纯逻辑：客户端持有 UDP socket）。
// 客户端负责：绑定 5353 组播、收到查询调 HandleQuery 应答、周期调 Announce 发送通告。
type MdnsAdvertisor struct {
	records []dnsRecord
}

// NewMdnsAdvertisor 组装本机的 mDNS 记录（不绑定 socket）。
func NewMdnsAdvertisor(instance, hostName string, ip net.IP, port uint16, txt MdnsTXTRecord) (*MdnsAdvertisor, error) {
	if err := txt.Validate(); err != nil {
		return nil, err
	}
	ttl := uint32(120)
	a := MdnsAdvertisor{
		records: []dnsRecord{
			{name: mdnsService, rtype: dnsTypePTR, class: dnsClassIN | dnsClassCacheFlsh,
				ttl: ttl, ptrTarget: instance},
			{name: instance, rtype: dnsTypeSRV, class: dnsClassIN | dnsClassCacheFlsh,
				ttl: ttl, srvHost: hostName, srvPort: port},
			{name: instance, rtype: dnsTypeTXT, class: dnsClassIN | dnsClassCacheFlsh,
				ttl: ttl, txt: txt.MarshalTXT()},
			{name: hostName, rtype: dnsTypeA, class: dnsClassIN | dnsClassCacheFlsh,
				ttl: ttl, addr: ip},
		},
	}
	return &a, nil
}

// HandleQuery 处理一条收到的 mDNS 查询；若是问本服务类型的 PTR 查询，返回应答字节，否则返回 nil。
func (a *MdnsAdvertisor) HandleQuery(data []byte) []byte {
	msg, err := parseDNSMessage(data)
	if err != nil || msg.header.flags&dnsFlagResponse != 0 {
		return nil
	}
	for _, q := range msg.questions {
		if q.qtype == dnsTypePTR &&
			strings.EqualFold(strings.TrimSuffix(q.name, "."), strings.TrimSuffix(mdnsService, ".")) {
			return a.buildResponsePacket()
		}
	}
	return nil
}

// Announce 产生一条主动通告字节（客户端经 UDP 多播周期发送）。
func (a *MdnsAdvertisor) Announce() []byte {
	return a.buildResponsePacket()
}

func (a *MdnsAdvertisor) buildResponsePacket() []byte {
	pkt := make([]byte, 0, 512)
	pkt = binary.BigEndian.AppendUint16(pkt, 0)                      // id
	pkt = binary.BigEndian.AppendUint16(pkt, dnsFlagResponse)        // flags
	pkt = binary.BigEndian.AppendUint16(pkt, 0)                      // qdcount
	pkt = binary.BigEndian.AppendUint16(pkt, uint16(len(a.records))) // ancount
	pkt = binary.BigEndian.AppendUint16(pkt, 0)                      // nscount
	pkt = binary.BigEndian.AppendUint16(pkt, 0)                      // arcount
	for _, rec := range a.records {
		pkt = append(pkt, rec.marshal()...)
	}
	return pkt
}
