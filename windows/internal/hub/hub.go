// 输入/剪贴板/文件路由中枢：把平台面（wininput）与协议会话（link）粘合。
//
// 入站 HID 帧 → SendInput 注入；本机捕获 → 托管对端转发（切换热键
// Ctrl+Alt+0..8）；剪贴板本地变更 → 广播在线对端（入站写回本机，回声抑制）；
// 文件入站自动接受落 downloads、出站经 IPC 发起。
package hub

import (
	"fmt"
	"hash/fnv"
	"log/slog"
	"os"
	"path/filepath"
	"sort"
	"sync"
	"time"

	"github.com/flowgrid/protocol"

	"github.com/flowgrid/windows/internal/link"
	"github.com/flowgrid/windows/internal/wininput"
)

// ClipEntry 是一条剪贴板历史（spec windows-clipboard-history）。
type ClipEntry struct {
	Text   string `json:"-"`
	Source string `json:"source"` // 对端名或「本机」
	Dir    string `json:"dir"`    // send | recv
	Size   int    `json:"size"`
	Time   string `json:"time"`
}

// ClipboardEntryMeta 是 clipboard.history 的条目投影（不含全文）。
type ClipboardEntryMeta struct {
	Index   int    `json:"index"`
	Preview string `json:"preview"`
	Source  string `json:"source"`
	Dir     string `json:"dir"`
	Size    int    `json:"size"`
	Time    string `json:"time"`
}

const (
	clipboardEchoWindow  = 3 * time.Second
	clipboardMaxSendText = 1 << 20 // 保守上限：超长文本直接拒发
	clipboardDefaultCap  = 50      // 设计稿「保留最近 50 条」
)

// InputStatus 是 input.status 的载荷。
type InputStatus struct {
	Hosting    string `json:"hosting"` // "" = 本机
	SwitchHint string `json:"switch_hint"`
	ClipSync   bool   `json:"clip_sync"`
	AutoAccept bool   `json:"auto_accept"`
}

// TransferInfo 是一条文件传输记录（双向）。
type TransferInfo struct {
	Dir   string `json:"dir"` // send | recv
	Peer  string `json:"peer"`
	Name  string `json:"name"`
	Sent  int    `json:"sent"`
	Total int    `json:"total"`
	Done  bool   `json:"done"`
	Err   string `json:"err,omitempty"`
	Path  string `json:"path,omitempty"` // recv：落盘路径
}

// KeymapStatus 是 keymap.status 的载荷。
type KeymapStatus struct {
	Loaded bool   `json:"loaded"`
	Path   string `json:"path"`
	Rules  int    `json:"rules"`
}

type Hub struct {
	eng        *link.Engine
	downloads  string
	keymapPath string

	mu            sync.Mutex
	hosting       string // 正在托管的对端名（"" = 本机）
	keymap        *protocol.KeyMapTable
	keymapRules   int
	transfers     []TransferInfo
	clipHistory   []ClipEntry
	clipCap       int
	clipSync      bool
	clipHash      uint64
	suppressUntil time.Time
	watch         map[int]func()
	nextWatch     int
	wg            sync.WaitGroup

	in       *wininput.Hooks
	clipStop func()
	// linkCancel 取消对 Engine 连接变化的订阅：托管对端掉线时自动回本机，
	// 并释放仍按在目标机上的键（否则断线时按着 Ctrl 会永久卡住）。
	linkCancel func()
	// onlinePeers 是上次通知时的在线集合，用来区分「新增连接」与「掉线」：
	// 任何一个对端掉线都可能留下按住的键（它也可能正在控制本机），
	// 所以掉线时无条件收尾；新增连接不需要。
	onlinePeers map[string]bool
	// outDown 记录已转发给当前托管对端、尚未收到本机 KEY_UP 的键：
	// 物理 HID → 实际发出去的 HID（keymap 可能改写）。切换托管时靠它补发
	// KEY_UP，否则旧对端会一直以为 Ctrl/Alt 还按着。
	outDown map[uint16]uint16
	// outButtons 记录已转发给当前托管对端、尚未松开的鼠标按钮（bit0..bit4）。
	outButtons uint8
}

func New(eng *link.Engine, downloads, keymapPath string) *Hub {
	return &Hub{
		eng:        eng,
		downloads:  downloads,
		keymapPath: keymapPath,
		clipCap:    clipboardDefaultCap,
		clipSync:   true,
		watch:      map[int]func(){},
	}
}

// Start 装配输入钩子、剪贴板监听与协议回调。输入钩子需要交互桌面；
// 在非交互会话（如服务上下文）启动失败属预期，调用方降级处理。
func (h *Hub) Start() error {
	h.eng.SetHandlers(h.onPeerFrame, h.onPeerClipboard, h.onFileOffer,
		h.onFileProgress, h.MarkSendDone, h.onFileComplete)
	h.loadKeymap()
	h.onlinePeers = h.onlineSet()
	h.linkCancel = h.eng.OnChange(h.onLinksChanged)

	in, err := wininput.StartHooks(h.isHosting)
	if err != nil {
		h.linkCancel()
		h.linkCancel = nil
		return fmt.Errorf("hub: input hooks: %w", err)
	}
	h.in = in
	h.wg.Add(1)
	go func() {
		defer h.wg.Done()
		h.consume(in.Events())
	}()

	clipStop, err := wininput.StartClipboardListener(h.onLocalClipboardChanged)
	if err != nil {
		in.Stop()
		h.in = nil
		if h.linkCancel != nil {
			h.linkCancel()
			h.linkCancel = nil
		}
		return fmt.Errorf("hub: clipboard listener: %w", err)
	}
	h.clipStop = clipStop
	return nil
}

func (h *Hub) Stop() {
	if h.linkCancel != nil {
		h.linkCancel()
		h.linkCancel = nil
	}
	if h.clipStop != nil {
		h.clipStop()
	}
	if h.in != nil {
		h.in.Stop()
	}
	h.wg.Wait()
	// 显式收尾：进程存活期间 SendInput 的按下状态不会因「设备消失」自动清。
	wininput.ReleaseAll()
	h.mu.Lock()
	h.hosting = ""
	h.outDown = nil
	h.outButtons = 0
	h.mu.Unlock()
}

// onLinksChanged 在连接状态变化时被调用（回调运行在 Engine 的 notify 线程，
// 且不持有 Engine 锁）。
//
// 两类收尾：
//   - 当前托管的对端掉线 → 回到本机；
//   - 任何对端掉线 → 松开本机注入过且仍按着的键。对端也可能正在控制本机，
//     hosting 为空并不代表没有入站输入；SendInput 没有「连接断开自动 keyUp」。
func (h *Hub) onLinksChanged() {
	now := h.onlineSet()

	h.mu.Lock()
	hosting := h.hosting
	prev := h.onlinePeers
	removed := false
	for name := range prev {
		if !now[name] {
			removed = true
			break
		}
	}
	h.onlinePeers = now
	cleared := false
	if hosting != "" && !now[hosting] {
		h.hosting = ""
		cleared = true
		// 托管对端已掉线：outDown/outButtons 无法再补发，清掉避免之后
		// 误发到新对端；其他对端掉线不影响当前托管的出站状态。
		h.outDown = nil
		h.outButtons = 0
	}
	h.mu.Unlock()

	if removed || cleared {
		wininput.ReleaseAll()
	}
	if cleared {
		h.notify()
	}
}

func (h *Hub) onlineSet() map[string]bool {
	names := h.eng.OnlinePeerNames()
	set := make(map[string]bool, len(names))
	for _, name := range names {
		set[name] = true
	}
	return set
}

// ── 入站注入 ────────────────────────────────────────────────────────────

func (h *Hub) onPeerFrame(peer string, f *protocol.HIDFrame) {
	var err error
	switch f.Header.FrameType {
	case protocol.FrameKeyDown:
		if p, e := protocol.UnmarshalKeyDown(f.Payload); e == nil {
			err = wininput.InjectKey(p.KeyCode, true, p.Modifiers)
		}
	case protocol.FrameKeyUp:
		if p, e := protocol.UnmarshalKeyUp(f.Payload); e == nil {
			err = wininput.InjectKey(p.KeyCode, false, 0)
		}
	case protocol.FrameMouseMove:
		if p, e := protocol.UnmarshalMouseMove(f.Payload); e == nil {
			err = wininput.InjectMouseMove(p.DeltaX, p.DeltaY)
		}
	case protocol.FrameMouseButton:
		if p, e := protocol.UnmarshalMouseButton(f.Payload); e == nil {
			err = wininput.InjectMouseButton(p.ButtonID, p.State == 1)
		}
	case protocol.FrameScroll:
		if p, e := protocol.UnmarshalScroll(f.Payload); e == nil {
			err = wininput.InjectScroll(p.DeltaY, p.DeltaX)
		}
	}
	if err != nil {
		slog.Debug("hub: inject failed", "peer", peer, "err", err)
	}
}

// ── 捕获与切换 ──────────────────────────────────────────────────────────

func (h *Hub) isHosting() bool {
	h.mu.Lock()
	defer h.mu.Unlock()
	return h.hosting != ""
}

func (h *Hub) Hosting() string {
	h.mu.Lock()
	defer h.mu.Unlock()
	return h.hosting
}

func (h *Hub) Status() InputStatus {
	h.mu.Lock()
	defer h.mu.Unlock()
	return InputStatus{
		Hosting:    h.hosting,
		SwitchHint: "Ctrl+Alt+1..8 托管在线设备，Ctrl+Alt+0 回本机",
		ClipSync:   h.clipSync,
		AutoAccept: h.eng.AutoAccept(),
	}
}

// SwitchTo 切换托管：0 = 回本机，1..8 = 在线对端（实例名序）。
func (h *Hub) SwitchTo(index int) error {
	if index < 0 || index > 8 {
		return fmt.Errorf("hub: switch index out of range 0..8: %d", index)
	}
	next := ""
	if index > 0 {
		names := h.eng.OnlinePeerNames()
		if index > len(names) {
			return fmt.Errorf("hub: no online peer #%d (have %d)", index, len(names))
		}
		next = names[index-1]
	}

	h.mu.Lock()
	old := h.hosting
	changed := old != next
	h.mu.Unlock()
	if changed {
		// 先把旧对端仍按着的键松开：切换后本机的 KEY_UP 会发往新对端，
		// 旧对端永远等不到，Ctrl/Alt 会卡在按下状态。
		if old != "" {
			h.releaseOutbound(old)
		}
	}

	h.mu.Lock()
	h.hosting = next
	h.mu.Unlock()
	if changed {
		wininput.ReleaseAll()
	}
	h.notify()
	return nil
}

// releaseOutbound 给指定对端补发本机仍按着的键的 KEY_UP。
// 调用方保证 outDown 里的键确实发给了该对端（当前托管目标）。
func (h *Hub) releaseOutbound(peer string) {
	h.mu.Lock()
	sent := make([]uint16, 0, len(h.outDown))
	for _, hid := range h.outDown {
		sent = append(sent, hid)
	}
	buttons := h.outButtons
	h.outDown = nil
	h.outButtons = 0
	h.mu.Unlock()
	if len(sent) == 0 && buttons == 0 {
		return
	}
	for button := uint8(1); button <= 5; button++ {
		if buttons&(1<<(button-1)) != 0 {
			_ = h.eng.SendToPeer(peer, protocol.FrameMouseButton,
				(&protocol.MouseButtonPayload{ButtonID: button, State: 0}).Marshal())
		}
	}
	// 先松普通键再松修饰键：让应用看到完整的「组合键结束」。
	sort.SliceStable(sent, func(i, j int) bool {
		return protocol.ModifierFromSingleKey(sent[i]) == 0 &&
			protocol.ModifierFromSingleKey(sent[j]) != 0
	})
	for _, hid := range sent {
		_ = h.eng.SendToPeer(peer, protocol.FrameKeyUp,
			(&protocol.KeyUpPayload{KeyCode: hid}).Marshal())
	}
}

func (h *Hub) consume(events <-chan wininput.InputEvent) {
	for ev := range events {
		if ev.Kind == wininput.KindKey && ev.Down {
			if digit, isDigit := switchDigit(ev.HID); isDigit &&
				ev.Modifiers&protocol.ModCtrl != 0 && ev.Modifiers&protocol.ModAlt != 0 {
				if err := h.SwitchTo(digit); err != nil {
					slog.Debug("hub: switch refused", "err", err)
				}
				continue
			}
		}

		hosting := h.Hosting()
		if hosting == "" {
			continue // 非托管态：本机输入直通，钩子未吞
		}
		h.forward(hosting, ev)
	}
}

func (h *Hub) forward(hosting string, ev wininput.InputEvent) {
	peerPlatform := h.eng.PeerPlatform(hosting)
	hid := ev.HID
	var mods uint8
	if h.keymap != nil {
		ctx := protocol.AppContext(appName(wininput.ForegroundApp()))
		// 掩码和键位必须用同一套规则换算：用户可能把 Win 映射成 Alt，
		// 只按平台默认换掩码会出现「按的是 Alt、掩码说 Ctrl」。
		mods = h.keymap.RemapModifierMask(protocol.PlatformWindows, peerPlatform, ev.Modifiers, ctx)
		if nk, nm, ok := h.keymap.Lookup(protocol.PlatformWindows, peerPlatform, ev.HID, mods, ctx); ok {
			hid, mods = nk, nm
		} else {
			// 用户规则没命中时走平台默认换算：修饰键的 HID 也要换，
			// 否则 mac 目标端收到的是 Control 键而不是 Command（§6.3）。
			hid = protocol.RemapModifierKey(hid, protocol.PlatformWindows, peerPlatform)
		}
	} else {
		mods = protocol.RemapModifiers(ev.Modifiers, protocol.PlatformWindows, peerPlatform)
		hid = protocol.RemapModifierKey(hid, protocol.PlatformWindows, peerPlatform)
	}

	if ev.Kind == wininput.KindKey {
		h.mu.Lock()
		if h.outDown == nil {
			h.outDown = make(map[uint16]uint16)
		}
		if ev.Down {
			h.outDown[ev.HID] = hid
		} else {
			delete(h.outDown, ev.HID)
		}
		h.mu.Unlock()
	} else if ev.Kind == wininput.KindMouseButton && ev.Button >= 1 && ev.Button <= 5 {
		h.mu.Lock()
		bit := uint8(1) << (ev.Button - 1)
		if ev.Down {
			h.outButtons |= bit
		} else {
			h.outButtons &^= bit
		}
		h.mu.Unlock()
	}

	var err error
	switch ev.Kind {
	case wininput.KindKey:
		if ev.Down {
			err = h.eng.SendToPeer(hosting, protocol.FrameKeyDown,
				(&protocol.KeyDownPayload{KeyCode: hid, Modifiers: mods}).Marshal())
		} else {
			err = h.eng.SendToPeer(hosting, protocol.FrameKeyUp,
				(&protocol.KeyUpPayload{KeyCode: hid}).Marshal())
		}
	case wininput.KindMouseMove:
		err = h.eng.SendToPeer(hosting, protocol.FrameMouseMove,
			(&protocol.MouseMovePayload{DeltaX: ev.DX, DeltaY: ev.DY}).Marshal())
	case wininput.KindMouseButton:
		state := uint8(0)
		if ev.Down {
			state = 1
		}
		err = h.eng.SendToPeer(hosting, protocol.FrameMouseButton,
			(&protocol.MouseButtonPayload{ButtonID: ev.Button, State: state}).Marshal())
	case wininput.KindWheel:
		err = h.eng.SendToPeer(hosting, protocol.FrameScroll,
			(&protocol.ScrollPayload{DeltaY: ev.DeltaY, DeltaX: ev.DX}).Marshal())
	}
	if err != nil {
		if ev.Kind == wininput.KindKey && ev.Down {
			h.mu.Lock()
			delete(h.outDown, ev.HID)
			h.mu.Unlock()
		}
		if ev.Kind == wininput.KindMouseButton && ev.Down && ev.Button >= 1 && ev.Button <= 5 {
			h.mu.Lock()
			h.outButtons &^= uint8(1) << (ev.Button - 1)
			h.mu.Unlock()
		}
		slog.Debug("hub: forward failed", "peer", hosting, "err", err)
	}
}

// switchDigit Ctrl+Alt+数字 → 0..9（HIDKey1..9 = 0x1E..0x26，HIDKey0 = 0x27）。
func switchDigit(hid uint16) (int, bool) {
	switch {
	case hid >= protocol.HIDKey1 && hid <= protocol.HIDKey9:
		return int(hid - protocol.HIDKey1 + 1), true
	case hid == protocol.HIDKey0:
		return 0, true
	}
	return 0, false
}

func appName(exePath string) string {
	if exePath == "" {
		return ""
	}
	return filepath.Base(exePath)
}

// ── 剪贴板 ──────────────────────────────────────────────────────────────

func clipHash(text string) uint64 {
	f := fnv.New64a()
	_, _ = f.Write([]byte(text))
	return f.Sum64()
}

// onLocalClipboardChanged 本机剪贴板变了（用户复制）→ 广播给在线对端。
func (h *Hub) onLocalClipboardChanged() {
	text, ok := wininput.ReadClipboardText()
	if !ok {
		return
	}
	h.broadcastClipboard(text)
}

// SetClipboardText IPC 直发（冒烟/粘贴按钮）：等效「用户刚复制了这段文本」。
func (h *Hub) SetClipboardText(text string) error {
	if len(text) > clipboardMaxSendText {
		return fmt.Errorf("hub: clipboard text too large: %d", len(text))
	}
	h.broadcastClipboard(text)
	return nil
}

func (h *Hub) broadcastClipboard(text string) {
	if !h.ClipSyncEnabled() {
		return // 跨端同步关闭：本地变更不广播、不入史
	}
	if len(text) > protocol.MaxClipboardSize {
		slog.Warn("hub: clipboard exceeds protocol cap, not sent", "bytes", len(text))
		return
	}
	hash := clipHash(text)
	h.mu.Lock()
	if time.Now().Before(h.suppressUntil) && hash == h.clipHash {
		h.mu.Unlock()
		return // 自己刚写进去的回声
	}
	h.clipHash = hash
	h.mu.Unlock()
	h.recordClip(text, "本机", "send")
	h.eng.BroadcastClipboard("text/plain", []byte(text))
}

// ── 剪贴板历史（spec windows-clipboard-history） ────────────────────────

func (h *Hub) recordClip(text, source, dir string) {
	h.mu.Lock()
	h.clipHistory = append([]ClipEntry{{
		Text: text, Source: source, Dir: dir,
		Size: len(text), Time: time.Now().Format("15:04:05"),
	}}, h.clipHistory...)
	if len(h.clipHistory) > h.clipCap {
		h.clipHistory = h.clipHistory[:h.clipCap]
	}
	h.mu.Unlock()
	h.notify()
}

func previewOf(text string) string {
	firstLine := text
	if i := indexNewline(text); i >= 0 {
		firstLine = text[:i]
	}
	r := []rune(firstLine)
	if len(r) > 80 {
		firstLine = string(r[:80]) + "…"
	}
	return firstLine
}

func indexNewline(s string) int {
	for i := 0; i < len(s); i++ {
		if s[i] == '\n' {
			return i
		}
	}
	return -1
}

func (h *Hub) ClipboardHistory() []ClipboardEntryMeta {
	h.mu.Lock()
	defer h.mu.Unlock()
	out := make([]ClipboardEntryMeta, 0, len(h.clipHistory))
	for i, e := range h.clipHistory {
		out = append(out, ClipboardEntryMeta{
			Index: i, Preview: previewOf(e.Text), Source: e.Source,
			Dir: e.Dir, Size: e.Size, Time: e.Time,
		})
	}
	return out
}

func (h *Hub) ClipboardEntryText(index int) (string, bool) {
	h.mu.Lock()
	defer h.mu.Unlock()
	if index < 0 || index >= len(h.clipHistory) {
		return "", false
	}
	return h.clipHistory[index].Text, true
}

func (h *Hub) ClearClipboardHistory() {
	h.mu.Lock()
	h.clipHistory = nil
	h.mu.Unlock()
	h.notify()
}

// WriteClipboardEntry 「写入本地」：取该条全文写 OS 剪贴板（抑制回声广播）。
func (h *Hub) WriteClipboardEntry(index int) error {
	text, ok := h.ClipboardEntryText(index)
	if !ok {
		return fmt.Errorf("hub: no such clipboard entry %d", index)
	}
	h.mu.Lock()
	h.clipHash = clipHash(text)
	h.suppressUntil = time.Now().Add(clipboardEchoWindow)
	h.mu.Unlock()
	return wininput.WriteClipboardText(text)
}

func (h *Hub) SetClipboardCap(n int) {
	if n < 10 {
		n = 10
	}
	if n > 200 {
		n = 200
	}
	h.mu.Lock()
	h.clipCap = n
	if len(h.clipHistory) > n {
		h.clipHistory = h.clipHistory[:n]
	}
	h.mu.Unlock()
}

func (h *Hub) SetClipSync(enabled bool) {
	h.mu.Lock()
	h.clipSync = enabled
	h.mu.Unlock()
	h.notify()
}

func (h *Hub) ClipSyncEnabled() bool {
	h.mu.Lock()
	defer h.mu.Unlock()
	return h.clipSync
}

// ClipboardCount 供托盘/设置屏的入口计数。
func (h *Hub) ClipboardCount() int {
	h.mu.Lock()
	defer h.mu.Unlock()
	return len(h.clipHistory)
}

// ── 设置联动（autoaccept / 信任管理，委托引擎） ─────────────────────────

func (h *Hub) SetAutoAccept(enabled bool) { h.eng.SetAutoAccept(enabled) }

func (h *Hub) AutoAccept() bool { return h.eng.AutoAccept() }

func (h *Hub) TrustEntries() []link.TrustView { return h.eng.TrustEntries() }

func (h *Hub) TrustRevoke(fingerprint string) error { return h.eng.TrustRevoke(fingerprint) }

// onPeerClipboard 对端剪贴板重组完成 → 写回本机（抑制自己的监听回声）。
func (h *Hub) onPeerClipboard(peer string, c protocol.Clipboard) {
	if !h.ClipSyncEnabled() {
		return
	}
	if c.MIMEType != "text/plain" {
		return
	}
	text := string(c.Data)
	h.mu.Lock()
	h.clipHash = clipHash(text)
	h.suppressUntil = time.Now().Add(clipboardEchoWindow)
	h.mu.Unlock()
	h.recordClip(text, peer, "recv")
	if err := wininput.WriteClipboardText(text); err != nil {
		slog.Debug("hub: clipboard write failed", "peer", peer, "err", err)
	}
}

// ClipboardText 读本地剪贴板文本（端到端断言用）。
func (h *Hub) ClipboardText() (string, bool) {
	return wininput.ReadClipboardText()
}

// ── 文件 ────────────────────────────────────────────────────────────────

// SendFile 经 IPC 发起文件传输（≤16MiB，spec §7.4）。
func (h *Hub) SendFile(instance, path string) error {
	fi, err := os.Stat(path)
	if err != nil {
		return err
	}
	if fi.Size() > protocol.MaxFileSize || fi.Size() == 0 {
		return fmt.Errorf("hub: file size %d out of range (1..%d)", fi.Size(), protocol.MaxFileSize)
	}
	data, err := os.ReadFile(path)
	if err != nil {
		return err
	}
	peer := link.NameFromInstance(instance)
	h.mu.Lock()
	h.transfers = append(h.transfers, TransferInfo{
		Dir: "send", Peer: peer, Name: filepath.Base(path), Total: len(data),
	})
	h.mu.Unlock()
	h.notify()
	return h.eng.SendFileToPeer(peer, filepath.Base(path), data)
}

// onFileOffer 入站 OFFER：登记接收记录（自动接受发生在 link 会话内）。
func (h *Hub) onFileOffer(peer string, offer protocol.FileOffer) {
	h.mu.Lock()
	h.transfers = append(h.transfers, TransferInfo{
		Dir: "recv", Peer: peer, Name: offer.Name, Total: int(offer.Size),
	})
	h.mu.Unlock()
	h.notify()
}

// onFileProgress 出站块级进度（接收侧只有 OFFER 与完成两个事件）。
func (h *Hub) onFileProgress(peer, dir string, sent, total int) {
	h.mu.Lock()
	for i := range h.transfers {
		t := &h.transfers[i]
		if t.Dir == dir && t.Peer == peer && !t.Done {
			t.Sent, t.Total = sent, total
		}
	}
	h.mu.Unlock()
	h.notify()
}

func (h *Hub) onFileComplete(peer string, offer protocol.FileOffer, data []byte, ferr error) {
	if err := os.MkdirAll(h.downloads, 0o755); err != nil {
		slog.Warn("hub: mkdir downloads failed", "err", err)
		return
	}
	path := uniquePath(h.downloads, offer.Name)
	var doneErr string
	if ferr != nil {
		doneErr = ferr.Error()
	} else if err := os.WriteFile(path, data, 0o644); err != nil {
		doneErr = err.Error()
	}

	h.mu.Lock()
	for i := range h.transfers {
		t := &h.transfers[i]
		if t.Dir == "recv" && t.Peer == peer && t.Name == offer.Name && !t.Done {
			t.Done = true
			t.Sent = t.Total
			t.Err = doneErr
			t.Path = path
		}
	}
	h.mu.Unlock()
	h.notify()
	if doneErr != "" {
		slog.Warn("hub: inbound file failed", "peer", peer, "name", offer.Name, "err", doneErr)
	} else {
		slog.Info("hub: inbound file delivered", "peer", peer, "name", offer.Name, "path", path)
	}
}

// MarkSendDone 出站传输终态回调（由 main 从 OnDone 桥接；§7.4 每方向
// 同时只有一个传输，按 peer+dir 标记即可）。
func (h *Hub) MarkSendDone(peer string, err error) {
	h.mu.Lock()
	for i := range h.transfers {
		t := &h.transfers[i]
		if t.Dir == "send" && t.Peer == peer && !t.Done {
			t.Done = true
			if err != nil {
				t.Err = err.Error()
			} else {
				t.Sent = t.Total
			}
		}
	}
	h.mu.Unlock()
	h.notify()
}

func (h *Hub) Transfers() []TransferInfo {
	h.mu.Lock()
	defer h.mu.Unlock()
	out := make([]TransferInfo, len(h.transfers))
	copy(out, h.transfers)
	return out
}

// uniquePath downloads 目录内重名追加序号（a.txt → a(1).txt）。
func uniquePath(dir, name string) string {
	p := filepath.Join(dir, name)
	if _, err := os.Stat(p); os.IsNotExist(err) {
		return p
	}
	ext := filepath.Ext(name)
	base := name[:len(name)-len(ext)]
	for i := 1; ; i++ {
		p = filepath.Join(dir, fmt.Sprintf("%s(%d)%s", base, i, ext))
		if _, err := os.Stat(p); os.IsNotExist(err) {
			return p
		}
	}
}

// ── 键位映射 ────────────────────────────────────────────────────────────

func (h *Hub) loadKeymap() {
	kmf, err := protocol.LoadKeyMapFile(h.keymapPath)
	if err != nil {
		slog.Info("hub: no keymap rules loaded", "path", h.keymapPath)
		return
	}
	h.mu.Lock()
	h.keymap = protocol.NewKeyMapTable(kmf)
	h.keymapRules = len(kmf.Rules)
	h.mu.Unlock()
	slog.Info("hub: keymap rules loaded", "path", h.keymapPath, "rules", h.keymapRules)
}

func (h *Hub) KeymapStatus() KeymapStatus {
	h.mu.Lock()
	defer h.mu.Unlock()
	return KeymapStatus{Loaded: h.keymap != nil, Path: h.keymapPath, Rules: h.keymapRules}
}

// ── 订阅 ────────────────────────────────────────────────────────────────

func (h *Hub) OnChange(fn func()) (cancel func()) {
	h.mu.Lock()
	defer h.mu.Unlock()
	id := h.nextWatch
	h.nextWatch++
	h.watch[id] = fn
	return func() {
		h.mu.Lock()
		defer h.mu.Unlock()
		delete(h.watch, id)
	}
}

func (h *Hub) notify() {
	h.mu.Lock()
	fns := make([]func(), 0, len(h.watch))
	for _, f := range h.watch {
		fns = append(fns, f)
	}
	h.mu.Unlock()
	for _, f := range fns {
		f()
	}
}
