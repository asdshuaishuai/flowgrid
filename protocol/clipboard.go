package protocol

import (
	"sync"
	"time"
)

// ClipboardTimeout is how long the receiver keeps an incomplete clipboard
// without progress before dropping it (§5.3: 5 秒无进展丢弃整份). The clock
// re-anchors on every accepted chunk, so a slow-but-alive transfer is never
// killed mid-flight - only a stalled one is.
const ClipboardTimeout = 5 * time.Second

// Clipboard is a fully reassembled clipboard payload.
type Clipboard struct {
	MIMEType string
	Data     []byte
}

// ClipboardSession owns both directions of clipboard sync (§5.3).
//
// Send side: SendClipboard segments and transmits in order.
// Receive side: feed every inbound CLIPBOARD frame payload to HandleFrame;
// when a transfer completes, the clipboard is delivered through OnClipboard
// (GUI hook) and also returned from HandleFrame.
//
// The session is synchronous: no goroutines, no timers. Expiry is evaluated
// lazily on the next frame, driven by the injectable clock.
type ClipboardSession struct {
	emit func(frameType uint8, payload []byte) error

	// OnClipboard is invoked with every reassembled clipboard. Optional.
	OnClipboard func(Clipboard)

	mu        sync.Mutex
	now       func() time.Time
	tot       uint8
	mime      string
	totalSize uint32
	chunks    [][]byte
	received  int
	startedAt time.Time
}

// NewClipboardSession wires the session to a send seam (FrameStream.emit or
// any equivalent). One session per connection.
func NewClipboardSession(send func(frameType uint8, payload []byte) error) *ClipboardSession {
	return &ClipboardSession{
		emit: send,
		now:  time.Now,
	}
}

// SetClock overrides the wall clock (tests).
func (s *ClipboardSession) SetClock(now func() time.Time) {
	s.mu.Lock()
	s.now = now
	s.mu.Unlock()
}

// SendClipboard segments the clipboard and transmits every chunk in order
// (one pass). On a lossy transport prefer SendClipboardPasses.
func (s *ClipboardSession) SendClipboard(mime string, data []byte) error {
	return s.SendClipboardPasses(mime, data, 1)
}

// SendClipboardPasses segments the clipboard and transmits the full chunk
// stream `passes` times. The receiver deduplicates chunks by index, so extra
// passes are pure redundancy: a pass that loses chunks is repaired by the
// next one. passes < 1 is treated as 1.
func (s *ClipboardSession) SendClipboardPasses(mime string, data []byte, passes int) error {
	chunks, err := SegmentClipboard(mime, data)
	if err != nil {
		return err
	}
	if passes < 1 {
		passes = 1
	}
	for p := 0; p < passes; p++ {
		for i := range chunks {
			payload, err := chunks[i].Marshal()
			if err != nil {
				return err
			}
			if err := s.emit(FrameClipboard, payload); err != nil {
				return err
			}
		}
	}
	return nil
}

// HandleFrame processes one inbound CLIPBOARD payload. When the frame
// completes a transfer it returns the reassembled clipboard (and invokes
// OnClipboard); otherwise it returns nil, nil.
//
// Network-turbulence behaviour: duplicate chunks are ignored; chunks arriving
// out of order are buffered by index; a transfer expires after
// ClipboardTimeout without progress; and an orphan chunk (non-zero index with
// no active buffer - a late duplicate after completion or expiry) is silently
// dropped. Only a FragIdx=0 chunk may start a new buffer; a FragIdx=0 chunk
// with a different shape supersedes an incomplete one.
func (s *ClipboardSession) HandleFrame(payload []byte) (*Clipboard, error) {
	chunk, err := UnmarshalClipboard(payload)
	if err != nil {
		return nil, err
	}

	s.mu.Lock()
	now := s.now()
	if s.chunks != nil && now.Sub(s.startedAt) > ClipboardTimeout {
		s.resetLocked()
	}
	if s.chunks != nil && (chunk.FragTot != s.tot || chunk.MIMEType != s.mime || chunk.TotalSize != s.totalSize) {
		if chunk.FragIdx == 0 {
			// A new transfer supersedes the incomplete one (senders serialize
			// clipboard transfers, §5.3).
			s.resetLocked()
		} else {
			s.mu.Unlock()
			return nil, NewError(ErrInvalidPayload, "CLIPBOARD chunk does not match active transfer")
		}
	}
	if s.chunks == nil {
		// No active buffer: only the first chunk may open one. A non-zero
		// index here is a late duplicate (transfer completed or expired) -
		// ordinary network noise, drop silently.
		if chunk.FragIdx != 0 {
			s.mu.Unlock()
			return nil, nil
		}
		s.tot = chunk.FragTot
		s.mime = chunk.MIMEType
		s.totalSize = chunk.TotalSize
		s.chunks = make([][]byte, chunk.FragTot)
		s.startedAt = now
	}

	if s.chunks[chunk.FragIdx] != nil {
		s.mu.Unlock()
		return nil, nil // duplicate chunk, ignore
	}
	s.chunks[chunk.FragIdx] = chunk.Data
	s.received++
	s.startedAt = now // progress re-anchors the expiry clock

	var clip *Clipboard
	if s.received >= int(s.tot) {
		// Complete: assemble in index order and validate the total size.
		assembled := make([]byte, 0, s.totalSize)
		for _, c := range s.chunks {
			assembled = append(assembled, c...)
		}
		if uint64(len(assembled)) != uint64(s.totalSize) {
			s.resetLocked()
			s.mu.Unlock()
			return nil, NewError(ErrInvalidPayload, "CLIPBOARD reassembled size mismatch")
		}
		clip = &Clipboard{MIMEType: s.mime, Data: assembled}
		s.resetLocked()
	}
	s.mu.Unlock()

	// Deliver outside the lock so the callback may re-enter the session.
	if clip != nil && s.OnClipboard != nil {
		s.OnClipboard(*clip)
	}
	return clip, nil
}

func (s *ClipboardSession) resetLocked() {
	s.tot = 0
	s.mime = ""
	s.totalSize = 0
	s.chunks = nil
	s.received = 0
	s.startedAt = time.Time{}
}
