package protocol

import (
	"time"
)

// FrameSink abstracts the transport send path. Clients bind their own
// transport (DTLS socket, TCP stream, ...) to it: turn the sink's frame into
// wire bytes with EncodeBytesWithHMAC and hand them to the connection.
type FrameSink func(frame *HIDFrame) error

// FrameStream is the shared send seam for the session modules (clipboard,
// file transfer): it assigns Sequence/Timestamp and hands encoded frames to
// the sink. It deliberately does not own the transport — the client keeps
// control of sockets, reconnection and encryption.
type FrameStream struct {
	// Seq assigns frame sequence numbers; may be nil (frames go out with
	// sequence 0) but a real connection should share one tracker with the
	// heartbeat/latency paths.
	Seq *SequenceTracker
	// Now returns microseconds for the frame timestamp; nil uses wall clock.
	Now func() uint64
	// Send is the transport sink; required.
	Send FrameSink
}

// NewFrameStream wires a sink and an optional sequence tracker together.
func NewFrameStream(send FrameSink, seq *SequenceTracker) *FrameStream {
	return &FrameStream{Seq: seq, Send: send}
}

// NowUs returns the configured (or wall) clock in microseconds.
func (s *FrameStream) NowUs() uint64 {
	if s.Now != nil {
		return s.Now()
	}
	return uint64(time.Now().UnixMicro())
}

// Emit fills in sequence/timestamp and pushes the frame to the transport.
// This is the function to hand to the session modules:
//
//	stream := protocol.NewFrameStream(sink, seq)
//	clip := protocol.NewClipboardSession(stream.Emit)
//	files := protocol.NewFileSender(stream.Emit)
func (s *FrameStream) Emit(frameType uint8, payload []byte) error {
	seq := uint16(0)
	if s.Seq != nil {
		seq = s.Seq.Next()
	}
	f := &HIDFrame{
		Header: FrameHeader{
			FrameType: frameType,
			Sequence:  seq,
			Timestamp: s.NowUs(),
		},
		Payload: payload,
	}
	return s.Send(f)
}
