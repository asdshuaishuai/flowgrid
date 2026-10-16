package protocol

import (
	"crypto/sha256"
	"encoding/binary"
	"fmt"
	"path"
	"strings"
	"sync"
	"time"
)

// File transfer limits and timers (spec section 7.4).
const (
	// MaxFileSize bounds one transfer. The UInt8 frame payload caps a chunk at
	// 249 bytes, so even 16 MiB takes ~67k frames - file transfer targets
	// documents and screenshots, not media; larger payloads need a future
	// frame format with a wider length field.
	MaxFileSize = 16 << 20 // 16 MiB

	// fileChunkOverhead is the FILE_CHUNK fixed part: TransferID(2) + ChunkIdx(4).
	fileChunkOverhead = 6
	// MaxFileChunk is the largest FILE_CHUNK data section.
	MaxFileChunk = int(MaxPayloadLen) - fileChunkOverhead // 249

	// FileOfferTimeout: an unacknowledged offer expires after this long.
	FileOfferTimeout = 30 * time.Second
	// FileIdleTimeout: a transfer with no chunk progress for this long fails.
	FileIdleTimeout = 10 * time.Second
)

// FileAckStatus is carried by FILE_ACK (spec section 7.4).
type FileAckStatus uint8

const (
	FileAccepted     FileAckStatus = 0x00 // accept, start streaming
	FileRejectedBusy FileAckStatus = 0x01 // reject: another transfer in progress
	FileRejectedUser FileAckStatus = 0x02 // reject: declined by the user
	FileTooLarge     FileAckStatus = 0x03 // reject: exceeds the size limit
	FileHashMismatch FileAckStatus = 0x04 // finished: SHA-256 mismatch
	FileComplete     FileAckStatus = 0x05 // finished: verified
)

// FileAbortReason is carried by FILE_ABORT (spec section 7.4).
type FileAbortReason uint8

const (
	FileAbortUser    FileAbortReason = 0x00
	FileAbortError   FileAbortReason = 0x01
	FileAbortUnknown FileAbortReason = 0xFF
)

// --- Wire formats (spec section 7.4) ---

// FileOffer is the FILE_OFFER (0x21) payload.
type FileOffer struct {
	TransferID uint16
	Size       uint64
	SHA256     [32]byte
	Name       string // file name only, no path components
}

// MaxFileNameLen is the longest name a FILE_OFFER can carry on the wire.
const MaxFileNameLen = int(MaxPayloadLen) - 43 // 2+8+32 fixed + 1 name-length byte

func (o *FileOffer) Validate() error {
	if o.Size == 0 || o.Size > MaxFileSize {
		return NewError(ErrInvalidPayload, fmt.Sprintf("file size %d out of range (1..%d)", o.Size, MaxFileSize))
	}
	if len(o.Name) == 0 || len(o.Name) > MaxFileNameLen {
		return NewError(ErrInvalidPayload, fmt.Sprintf("file name length %d out of range (1..%d)", len(o.Name), MaxFileNameLen))
	}
	if strings.ContainsRune(o.Name, '/') || strings.ContainsRune(o.Name, '\\') || path.Clean(o.Name) != o.Name ||
		o.Name == "." || o.Name == ".." {
		return NewError(ErrInvalidPayload, "file name must not contain path components")
	}
	return nil
}

func (o *FileOffer) Marshal() ([]byte, error) {
	if err := o.Validate(); err != nil {
		return nil, err
	}
	buf := make([]byte, 43+len(o.Name))
	binary.BigEndian.PutUint16(buf[0:2], o.TransferID)
	binary.BigEndian.PutUint64(buf[2:10], o.Size)
	copy(buf[10:42], o.SHA256[:])
	buf[42] = uint8(len(o.Name))
	copy(buf[43:], o.Name)
	return buf, nil
}

func UnmarshalFileOffer(data []byte) (*FileOffer, error) {
	if len(data) < 43 {
		return nil, NewError(ErrInvalidPayload, "FILE_OFFER payload too short")
	}
	o := &FileOffer{
		TransferID: binary.BigEndian.Uint16(data[0:2]),
		Size:       binary.BigEndian.Uint64(data[2:10]),
	}
	copy(o.SHA256[:], data[10:42])
	nameLen := int(data[42])
	if len(data) < 43+nameLen {
		return nil, NewError(ErrInvalidPayload, "FILE_OFFER name truncated")
	}
	o.Name = string(data[43 : 43+nameLen])
	if err := o.Validate(); err != nil {
		return nil, err
	}
	return o, nil
}

// FileAck is the FILE_ACK (0x22) payload.
type FileAck struct {
	TransferID uint16
	Status     FileAckStatus
}

func (a *FileAck) Marshal() []byte {
	buf := make([]byte, 3)
	binary.BigEndian.PutUint16(buf[0:2], a.TransferID)
	buf[2] = uint8(a.Status)
	return buf
}

func UnmarshalFileAck(data []byte) (*FileAck, error) {
	if len(data) < 3 {
		return nil, NewError(ErrInvalidPayload, "FILE_ACK payload too short")
	}
	return &FileAck{
		TransferID: binary.BigEndian.Uint16(data[0:2]),
		Status:     FileAckStatus(data[2]),
	}, nil
}

// FileChunk is the FILE_CHUNK (0x23) payload.
type FileChunk struct {
	TransferID uint16
	ChunkIdx   uint32
	Data       []byte
}

func (c *FileChunk) Marshal() ([]byte, error) {
	if len(c.Data) > MaxFileChunk {
		return nil, NewError(ErrInvalidPayload, fmt.Sprintf("FILE_CHUNK data %d exceeds %d bytes", len(c.Data), MaxFileChunk))
	}
	buf := make([]byte, fileChunkOverhead+len(c.Data))
	binary.BigEndian.PutUint16(buf[0:2], c.TransferID)
	binary.BigEndian.PutUint32(buf[2:6], c.ChunkIdx)
	copy(buf[6:], c.Data)
	return buf, nil
}

func UnmarshalFileChunk(data []byte) (*FileChunk, error) {
	if len(data) < fileChunkOverhead {
		return nil, NewError(ErrInvalidPayload, "FILE_CHUNK payload too short")
	}
	return &FileChunk{
		TransferID: binary.BigEndian.Uint16(data[0:2]),
		ChunkIdx:   binary.BigEndian.Uint32(data[2:6]),
		Data:       append([]byte(nil), data[6:]...),
	}, nil
}

// FileAbort is the FILE_ABORT (0x24) payload.
type FileAbort struct {
	TransferID uint16
	Reason     FileAbortReason
}

func (a *FileAbort) Marshal() []byte {
	buf := make([]byte, 3)
	binary.BigEndian.PutUint16(buf[0:2], a.TransferID)
	buf[2] = uint8(a.Reason)
	return buf
}

func UnmarshalFileAbort(data []byte) (*FileAbort, error) {
	if len(data) < 3 {
		return nil, NewError(ErrInvalidPayload, "FILE_ABORT payload too short")
	}
	return &FileAbort{
		TransferID: binary.BigEndian.Uint16(data[0:2]),
		Reason:     FileAbortReason(data[2]),
	}, nil
}

// --- Send side ---

type fileOutTransfer struct {
	offer     FileOffer
	data      []byte
	nextChunk uint32
	pass      int // completed retransmission passes
	created   time.Time
	lastMove  time.Time
}

// FileSender streams one file at a time (spec 7.4: one per direction).
//
// OfferFile sends FILE_OFFER and waits for the receiver's FILE_ACK (the GUI
// shows the accept prompt). HandleFrame consumes inbound FILE_ACK/FILE_ABORT.
// Pump transmits up to maxChunks queued FILE_CHUNK frames - call it from the
// event loop whenever the transport is writable.
type FileSender struct {
	emit func(frameType uint8, payload []byte) error

	// OnProgress reports bytes sent for the active transfer. Optional.
	OnProgress func(transferID uint16, sent, total int)
	// OnDone reports the terminal outcome. Optional.
	OnDone func(transferID uint16, err error)

	mu           sync.Mutex
	now          func() time.Time
	resendPasses int              // extra full retransmission passes (default 2)
	pending      *fileOutTransfer // offered, awaiting ACK
	active       *fileOutTransfer // accepted, streaming
}

// NewFileSender wires the sender to a send seam.
func NewFileSender(send func(frameType uint8, payload []byte) error) *FileSender {
	return &FileSender{emit: send, now: time.Now, resendPasses: 2}
}

// SetClock overrides the wall clock (tests).
func (s *FileSender) SetClock(now func() time.Time) {
	s.mu.Lock()
	s.now = now
	s.mu.Unlock()
}

// OfferFile starts a transfer of data under name. The file is hashed up
// front; the data stays in memory for the lifetime of the transfer.
func (s *FileSender) OfferFile(name string, data []byte) (uint16, error) {
	if len(data) == 0 || uint64(len(data)) > MaxFileSize {
		return 0, NewError(ErrInvalidPayload, fmt.Sprintf("file size %d out of range (1..%d)", len(data), MaxFileSize))
	}
	offer := FileOffer{
		Size: uint64(len(data)),
		Name: name,
	}
	offer.SHA256 = sha256.Sum256(data)

	s.mu.Lock()
	now := s.now()
	if s.pending != nil && now.Sub(s.pending.created) <= FileOfferTimeout {
		s.mu.Unlock()
		return 0, NewError(ErrInvalidPayload, "another offer is pending")
	}
	if s.active != nil && now.Sub(s.active.lastMove) <= FileIdleTimeout {
		s.mu.Unlock()
		return 0, NewError(ErrInvalidPayload, "another transfer is active")
	}
	// Retire stale state, then register the new offer.
	s.pending = nil
	id := uint16(now.UnixNano()) // arbitrary; uniqueness is per-direction
	for id == 0 {
		id++
	}
	offer.TransferID = id
	s.pending = &fileOutTransfer{offer: offer, data: data, created: now, lastMove: now}
	s.mu.Unlock()

	payload, err := offer.Marshal()
	if err != nil {
		s.mu.Lock()
		s.pending = nil
		s.mu.Unlock()
		return 0, err
	}
	if err := s.emit(FrameFileOffer, payload); err != nil {
		s.mu.Lock()
		s.pending = nil
		s.mu.Unlock()
		return 0, err
	}
	return id, nil
}

// fileProgress is one queued OnProgress report.
type fileProgress struct {
	id          uint16
	sent, total int
}

// Pump sends up to maxChunks FILE_CHUNK frames for the active transfer.
// Returns the number of frames sent. It is a no-op (0, nil) while the offer
// is unacknowledged and younger than FileOfferTimeout; it reports
// ErrTransferTimeout on both timeout paths (unacknowledged offer past
// FileOfferTimeout, and receiver silent past FileIdleTimeout). If the active
// transfer ends during one of the emit windows - completed, aborted, or
// replaced by a newer transfer - Pump stops without touching the new one.
//
// Loss recovery: after the last chunk of a pass, Pump wraps around and
// retransmits the file from chunk 0 for up to ResendPasses extra passes. The
// receiver deduplicates chunks by index, so retransmission is idempotent and
// repairs packets lost on lossy transports without any wire change.
func (s *FileSender) Pump(maxChunks int) (int, error) {
	if maxChunks <= 0 {
		return 0, nil
	}
	s.mu.Lock()
	now := s.now()
	t := s.active
	if t == nil {
		if s.pending != nil && now.Sub(s.pending.created) > FileOfferTimeout {
			id := s.pending.offer.TransferID
			s.pending = nil
			s.mu.Unlock()
			s.finish(&id, NewError(ErrTransferTimeout, "offer not acknowledged"))
			return 0, NewError(ErrTransferTimeout, "offer not acknowledged")
		}
		s.mu.Unlock()
		return 0, nil
	}
	if now.Sub(t.lastMove) > FileIdleTimeout {
		id := t.offer.TransferID
		s.active = nil
		s.mu.Unlock()
		s.finish(&id, NewError(ErrTransferTimeout, "receiver stopped accepting chunks"))
		return 0, NewError(ErrTransferTimeout, "receiver stopped accepting chunks")
	}

	started := t // identity anchor: emit windows may let the active transfer change
	totalChunks := uint32((len(t.data) + MaxFileChunk - 1) / MaxFileChunk)
	sent := 0
	var updates []fileProgress
	for sent < maxChunks {
		if t.nextChunk >= totalChunks {
			// Pass finished. Completed ACK may still be in flight; retransmit
			// (receiver dedupes) until the resend budget is spent.
			if t.pass > s.resendPasses {
				break
			}
			t.pass++
			t.nextChunk = 0
		}
		start := int(t.nextChunk) * MaxFileChunk
		end := start + MaxFileChunk
		if end > len(t.data) {
			end = len(t.data)
		}
		chunk := FileChunk{TransferID: t.offer.TransferID, ChunkIdx: t.nextChunk, Data: t.data[start:end]}
		payload, err := chunk.Marshal()
		if err != nil {
			s.mu.Unlock()
			return sent, err
		}
		s.mu.Unlock()
		if err := s.emit(FrameFileChunk, payload); err != nil {
			return sent, err
		}
		s.mu.Lock()
		t = s.active
		if t != started { // nil (done/aborted) or a superseding new transfer
			s.mu.Unlock()
			s.flushProgress(updates)
			return sent, nil
		}
		t.nextChunk++
		t.lastMove = s.now()
		sent++
		updates = append(updates, fileProgress{id: t.offer.TransferID, sent: int(t.nextChunk) * MaxFileChunk, total: int(totalChunks) * MaxFileChunk})
	}
	s.mu.Unlock()
	s.flushProgress(updates)
	return sent, nil
}

// SetResendPasses configures how many extra full retransmission passes Pump
// performs after the first one (default 2, i.e. up to 3 transmissions).
func (s *FileSender) SetResendPasses(n int) {
	if n < 0 {
		n = 0
	}
	s.mu.Lock()
	s.resendPasses = n
	s.mu.Unlock()
}

// flushProgress reports queued progress updates without the lock held, so the
// callback may safely re-enter the sender.
func (s *FileSender) flushProgress(updates []fileProgress) {
	if s.OnProgress == nil {
		return
	}
	for _, u := range updates {
		s.OnProgress(u.id, u.sent, u.total)
	}
}

// HandleFrame consumes an inbound FILE_ACK or FILE_ABORT payload.
func (s *FileSender) HandleFrame(frameType uint8, payload []byte) error {
	switch frameType {
	case FrameFileAck:
		ack, err := UnmarshalFileAck(payload)
		if err != nil {
			return err
		}
		return s.handleAck(ack)
	case FrameFileAbort:
		abort, err := UnmarshalFileAbort(payload)
		if err != nil {
			return err
		}
		s.mu.Lock()
		var id *uint16
		if s.active != nil && s.active.offer.TransferID == abort.TransferID {
			s.active = nil
			id = &abort.TransferID
		}
		s.mu.Unlock()
		if id != nil {
			s.finish(id, NewError(ErrTransferRejected, "transfer aborted by receiver"))
		}
		return nil
	default:
		return NewError(ErrInvalidPayload, fmt.Sprintf("FileSender: unexpected frame type 0x%02X", frameType))
	}
}

func (s *FileSender) handleAck(ack *FileAck) error {
	s.mu.Lock()
	now := s.now()
	switch ack.Status {
	case FileAccepted:
		if s.pending == nil || s.pending.offer.TransferID != ack.TransferID {
			s.mu.Unlock()
			return NewError(ErrInvalidPayload, "FILE_ACK for unknown transfer")
		}
		s.active = s.pending
		s.active.lastMove = now
		s.pending = nil
		s.mu.Unlock()
		return nil
	case FileComplete:
		if s.active == nil || s.active.offer.TransferID != ack.TransferID {
			s.mu.Unlock()
			return NewError(ErrInvalidPayload, "FILE_ACK for unknown transfer")
		}
		s.active = nil
		s.mu.Unlock()
		s.finish(&ack.TransferID, nil)
		return nil
	case FileRejectedBusy, FileRejectedUser, FileTooLarge:
		if s.pending != nil && s.pending.offer.TransferID == ack.TransferID {
			s.pending = nil
			s.mu.Unlock()
			s.finish(&ack.TransferID, NewError(ErrTransferRejected, "receiver rejected the file"))
			return nil
		}
		s.mu.Unlock()
		return NewError(ErrInvalidPayload, "FILE_ACK for unknown transfer")
	case FileHashMismatch:
		if s.active != nil && s.active.offer.TransferID == ack.TransferID {
			s.active = nil
			s.mu.Unlock()
			s.finish(&ack.TransferID, NewError(ErrInvalidPayload, "receiver reports hash mismatch"))
			return nil
		}
		s.mu.Unlock()
		return NewError(ErrInvalidPayload, "FILE_ACK for unknown transfer")
	default:
		s.mu.Unlock()
		return NewError(ErrInvalidPayload, fmt.Sprintf("unknown FILE_ACK status 0x%02X", uint8(ack.Status)))
	}
}

// finish reports the terminal outcome of a transfer to OnDone. Call without
// holding s.mu.
func (s *FileSender) finish(id *uint16, err error) {
	if s.OnDone != nil {
		s.OnDone(*id, err)
	}
}

// --- Receive side ---

type fileInTransfer struct {
	offer     FileOffer
	accepted  bool
	chunks    [][]byte
	received  int
	created   time.Time
	lastMove  time.Time
}

// FileReceiver receives one file at a time (spec 7.4).
//
// OnOffer fires when a peer offers a file - the GUI shows the prompt and the
// app calls Accept or Reject. Accepted chunks are buffered, and on the final
// chunk the SHA-256 is verified before OnComplete delivers the data.
type FileReceiver struct {
	emit func(frameType uint8, payload []byte) error

	// OnOffer fires for every inbound FILE_OFFER; the app must eventually
	// call Accept or Reject (GUI prompt). Required for transfers to proceed.
	OnOffer func(offer FileOffer)
	// OnComplete delivers a verified file, or the failure reason. Optional.
	OnComplete func(offer FileOffer, data []byte, err error)

	mu      sync.Mutex
	now     func() time.Time
	offered *fileInTransfer // prompt pending
	active  *fileInTransfer // accepted, receiving
}

// NewFileReceiver wires the receiver to a send seam (for ACK/ABORT frames).
func NewFileReceiver(send func(frameType uint8, payload []byte) error) *FileReceiver {
	return &FileReceiver{emit: send, now: time.Now}
}

// SetClock overrides the wall clock (tests).
func (r *FileReceiver) SetClock(now func() time.Time) {
	r.mu.Lock()
	r.now = now
	r.mu.Unlock()
}

// HandleFrame consumes an inbound FILE_OFFER / FILE_CHUNK / FILE_ABORT
// payload. handled reports whether the frame type belonged to the file
// transfer protocol (so a dispatcher can feed other types elsewhere).
func (r *FileReceiver) HandleFrame(frameType uint8, payload []byte) (handled bool, err error) {
	switch frameType {
	case FrameFileOffer:
		return true, r.handleOffer(payload)
	case FrameFileChunk:
		return true, r.handleChunk(payload)
	case FrameFileAbort:
		return true, r.handleAbort(payload)
	default:
		return false, nil
	}
}

func (r *FileReceiver) handleOffer(payload []byte) error {
	offer, err := UnmarshalFileOffer(payload)
	if err != nil {
		return err
	}

	r.mu.Lock()
	now := r.now()
	// A stalled sender must not wedge the receiver: expire an active transfer
	// that saw no chunk progress past FileIdleTimeout, then judge busyness.
	var stale *fileInTransfer
	if r.active != nil && now.Sub(r.active.lastMove) > FileIdleTimeout {
		stale = r.active
		r.active = nil
	}
	r.expireLocked(now)
	if r.offered != nil || r.active != nil {
		r.mu.Unlock()
		// One transfer at a time: politely decline while busy.
		return r.emit(FrameFileAck, (&FileAck{TransferID: offer.TransferID, Status: FileRejectedBusy}).Marshal())
	}
	r.offered = &fileInTransfer{offer: *offer, created: now, lastMove: now}
	onOffer := r.OnOffer
	r.mu.Unlock()

	if stale != nil {
		r.fail(stale, NewError(ErrTransferTimeout, "sender stopped sending chunks"))
	}
	if onOffer != nil {
		onOffer(*offer)
	}
	return nil
}

func (r *FileReceiver) handleChunk(payload []byte) error {
	chunk, err := UnmarshalFileChunk(payload)
	if err != nil {
		return err
	}

	r.mu.Lock()
	now := r.now()
	t := r.active
	if t == nil || t.offer.TransferID != chunk.TransferID || !t.accepted {
		// Unknown transfer: a late duplicate after completion, or a frame for
		// a transfer that already timed out - ordinary network noise, drop.
		r.mu.Unlock()
		return nil
	}
	if now.Sub(t.lastMove) > FileIdleTimeout {
		r.active = nil
		r.mu.Unlock()
		r.fail(t, NewError(ErrTransferTimeout, "sender stopped sending chunks"))
		return nil
	}
	if uint64(chunk.ChunkIdx) >= uint64(len(t.chunks)) {
		// Out of range: garbage or a stale chunk from a superseded offer.
		// uint64 comparison keeps the guard valid on 32-bit builds.
		r.mu.Unlock()
		return nil
	}
	if t.chunks[chunk.ChunkIdx] != nil {
		r.mu.Unlock()
		return nil // duplicate chunk, ignore
	}
	t.chunks[chunk.ChunkIdx] = chunk.Data
	t.received++
	t.lastMove = now
	finished := t.received == len(t.chunks)
	if finished {
		r.active = nil
	}
	r.mu.Unlock()

	if !finished {
		return nil
	}

	// Assemble and verify against the offer's hash before delivering.
	size := int(t.offer.Size)
	assembled := make([]byte, 0, size)
	for _, c := range t.chunks {
		assembled = append(assembled, c...)
	}
	if len(assembled) != size {
		r.fail(t, NewError(ErrInvalidPayload, "assembled size differs from offer"))
		return nil
	}
	if sha256.Sum256(assembled) != t.offer.SHA256 {
		r.emit(FrameFileAck, (&FileAck{TransferID: t.offer.TransferID, Status: FileHashMismatch}).Marshal())
		r.fail(t, NewError(ErrInvalidPayload, "SHA-256 mismatch"))
		return nil
	}
	r.emit(FrameFileAck, (&FileAck{TransferID: t.offer.TransferID, Status: FileComplete}).Marshal())
	if r.OnComplete != nil {
		r.OnComplete(t.offer, assembled, nil)
	}
	return nil
}

func (r *FileReceiver) handleAbort(payload []byte) error {
	abort, err := UnmarshalFileAbort(payload)
	if err != nil {
		return err
	}
	r.mu.Lock()
	var t *fileInTransfer
	if r.active != nil && r.active.offer.TransferID == abort.TransferID {
		t = r.active
		r.active = nil
	} else if r.offered != nil && r.offered.offer.TransferID == abort.TransferID {
		t = r.offered
		r.offered = nil
	}
	r.mu.Unlock()
	if t != nil {
		r.fail(t, NewError(ErrTransferRejected, "transfer aborted by sender"))
	}
	return nil
}

// Accept approves the pending offer and answers FILE_ACK accepted.
func (r *FileReceiver) Accept(transferID uint16) error {
	r.mu.Lock()
	t := r.offered
	if t == nil || t.offer.TransferID != transferID {
		r.mu.Unlock()
		return NewError(ErrInvalidPayload, "Accept: no such pending offer")
	}
	totalChunks := (int(t.offer.Size) + MaxFileChunk - 1) / MaxFileChunk
	t.chunks = make([][]byte, totalChunks)
	t.accepted = true
	t.lastMove = r.now()
	r.active = t
	r.offered = nil
	r.mu.Unlock()

	return r.emit(FrameFileAck, (&FileAck{TransferID: transferID, Status: FileAccepted}).Marshal())
}

// Reject declines the pending offer with a rejection status.
func (r *FileReceiver) Reject(transferID uint16, status FileAckStatus) error {
	if status != FileRejectedBusy && status != FileRejectedUser && status != FileTooLarge {
		return NewError(ErrInvalidPayload, "Reject: status is not a rejection")
	}
	r.mu.Lock()
	if r.offered != nil && r.offered.offer.TransferID == transferID {
		r.offered = nil
	}
	r.mu.Unlock()

	return r.emit(FrameFileAck, (&FileAck{TransferID: transferID, Status: status}).Marshal())
}

// Abort cancels the active (or pending) transfer and tells the peer.
func (r *FileReceiver) Abort(transferID uint16, reason FileAbortReason) error {
	r.mu.Lock()
	if r.active != nil && r.active.offer.TransferID == transferID {
		t := r.active
		r.active = nil
		r.mu.Unlock()
		r.fail(t, NewError(ErrTransferRejected, "transfer aborted locally"))
	} else if r.offered != nil && r.offered.offer.TransferID == transferID {
		r.offered = nil
		r.mu.Unlock()
	} else {
		r.mu.Unlock()
		return NewError(ErrInvalidPayload, "Abort: no such transfer")
	}
	return r.emit(FrameFileAbort, (&FileAbort{TransferID: transferID, Reason: reason}).Marshal())
}

// expireLocked drops a stale pending offer. Caller holds r.mu.
func (r *FileReceiver) expireLocked(now time.Time) {
	if r.offered != nil && now.Sub(r.offered.created) > FileOfferTimeout {
		r.offered = nil
	}
}

func (r *FileReceiver) fail(t *fileInTransfer, err error) {
	if r.OnComplete != nil {
		r.OnComplete(t.offer, nil, err)
	}
}
