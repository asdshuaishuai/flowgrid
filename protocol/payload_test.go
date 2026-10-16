package protocol

import (
	"bytes"
	"testing"
)

func TestKeyDownRoundTrip(t *testing.T) {
	orig := &KeyDownPayload{KeyCode: HIDKeyC, Modifiers: ModLeftCtrl}
	data := orig.Marshal()
	got, err := UnmarshalKeyDown(data)
	if err != nil {
		t.Fatal(err)
	}
	if got.KeyCode != orig.KeyCode || got.Modifiers != orig.Modifiers {
		t.Errorf("got %+v, want %+v", got, orig)
	}
}

func TestKeyUpRoundTrip(t *testing.T) {
	orig := &KeyUpPayload{KeyCode: HIDKeyEnter}
	data := orig.Marshal()
	got, err := UnmarshalKeyUp(data)
	if err != nil {
		t.Fatal(err)
	}
	if got.KeyCode != orig.KeyCode {
		t.Errorf("KeyCode: got 0x%04X, want 0x%04X", got.KeyCode, orig.KeyCode)
	}
}

func TestMouseMoveRoundTrip(t *testing.T) {
	orig := &MouseMovePayload{DeltaX: -32768, DeltaY: 32767}
	data := orig.Marshal()
	got, err := UnmarshalMouseMove(data)
	if err != nil {
		t.Fatal(err)
	}
	if got.DeltaX != orig.DeltaX || got.DeltaY != orig.DeltaY {
		t.Errorf("got %+v, want %+v", got, orig)
	}
}

func TestMouseButtonRoundTrip(t *testing.T) {
	orig := &MouseButtonPayload{ButtonID: MouseButtonRight, State: 1}
	data := orig.Marshal()
	got, err := UnmarshalMouseButton(data)
	if err != nil {
		t.Fatal(err)
	}
	if got.ButtonID != orig.ButtonID || got.State != orig.State {
		t.Errorf("got %+v, want %+v", got, orig)
	}
}

// MOUSE_BTN fields outside the §5.3 table must be rejected at decode time so
// garbage never reaches the injection layer.
func TestMouseButtonInvalidValuesRejected(t *testing.T) {
	if _, err := UnmarshalMouseButton([]byte{0x00, 0x01}); err == nil {
		t.Error("button id 0x00 must be rejected")
	}
	if _, err := UnmarshalMouseButton([]byte{0x06, 0x01}); err == nil {
		t.Error("button id 0x06 must be rejected")
	}
	if _, err := UnmarshalMouseButton([]byte{MouseButtonLeft, 0x02}); err == nil {
		t.Error("state 0x02 must be rejected")
	}
}

func TestScrollRoundTrip(t *testing.T) {
	orig := &ScrollPayload{DeltaY: 120, DeltaX: -60}
	data := orig.Marshal()
	got, err := UnmarshalScroll(data)
	if err != nil {
		t.Fatal(err)
	}
	if got.DeltaY != orig.DeltaY || got.DeltaX != orig.DeltaX {
		t.Errorf("got %+v, want %+v", got, orig)
	}
}

func TestClipboardRoundTrip(t *testing.T) {
	orig := &ClipboardPayload{
		MIMEType: "text/plain",
		FragTot:  1,
		Data:     []byte("Hello, FlowGrid!"),
	}
	data, err := orig.Marshal()
	if err != nil {
		t.Fatal(err)
	}
	got, err := UnmarshalClipboard(data)
	if err != nil {
		t.Fatal(err)
	}
	if got.MIMEType != orig.MIMEType {
		t.Errorf("MIMEType: got %q, want %q", got.MIMEType, orig.MIMEType)
	}
	if string(got.Data) != string(orig.Data) {
		t.Errorf("Data: got %q, want %q", got.Data, orig.Data)
	}
	if got.TotalSize != uint32(len(orig.Data)) {
		t.Errorf("TotalSize: got %d, want %d", got.TotalSize, len(orig.Data))
	}
	if got.FragIdx != 0 || got.FragTot != 1 {
		t.Errorf("fragment fields: got idx=%d tot=%d, want 0/1", got.FragIdx, got.FragTot)
	}
}

// The v1.1 segmented clipboard: SegmentClipboard must split a 64KB-class
// payload into per-frame chunks, each of which marshals under the 255-byte
// frame payload limit and reassembles byte-exactly.
func TestClipboardSegmentedRoundTrip(t *testing.T) {
	data := make([]byte, 60_000)
	for i := range data {
		data[i] = byte(i % 251)
	}
	chunks, err := SegmentClipboard("text/plain", data)
	if err != nil {
		t.Fatal(err)
	}
	if len(chunks) < 2 {
		t.Fatalf("expected segmentation, got %d chunks", len(chunks))
	}

	var assembled []byte
	for i, c := range chunks {
		if c.FragIdx != uint8(i) || c.FragTot != uint8(len(chunks)) {
			t.Fatalf("chunk %d: idx=%d tot=%d", i, c.FragIdx, c.FragTot)
		}
		wire, err := c.Marshal()
		if err != nil {
			t.Fatalf("chunk %d marshal: %v", i, err)
		}
		if len(wire) > int(MaxPayloadLen) {
			t.Fatalf("chunk %d payload %d exceeds frame limit %d", i, len(wire), MaxPayloadLen)
		}
		got, err := UnmarshalClipboard(wire)
		if err != nil {
			t.Fatalf("chunk %d unmarshal: %v", i, err)
		}
		if got.TotalSize != uint32(len(data)) {
			t.Fatalf("chunk %d: total size %d, want %d", i, got.TotalSize, len(data))
		}
		assembled = append(assembled, got.Data...)
	}
	if !bytes.Equal(assembled, data) {
		t.Error("reassembled clipboard differs from original")
	}
}

func TestClipboardMarshalRejectsBadFragment(t *testing.T) {
	if _, err := (&ClipboardPayload{MIMEType: "t", Data: []byte("x")}).Marshal(); err == nil {
		t.Error("FragTot=0 must be rejected")
	}
	if _, err := (&ClipboardPayload{MIMEType: "t", FragIdx: 2, FragTot: 1, Data: []byte("x")}).Marshal(); err == nil {
		t.Error("FragIdx >= FragTot must be rejected")
	}
	bigChunk := make([]byte, MaxClipboardChunk(len("text/plain"))+1)
	if _, err := (&ClipboardPayload{MIMEType: "text/plain", FragTot: 1, Data: bigChunk}).Marshal(); err == nil {
		t.Error("chunk exceeding the per-frame budget must be rejected")
	}
}

func TestClipboardMIMETooLong(t *testing.T) {
	longMIME := make([]byte, 256)
	for i := range longMIME {
		longMIME[i] = 'a'
	}
	p := &ClipboardPayload{MIMEType: string(longMIME), FragTot: 1, Data: []byte("x")}
	if _, err := p.Marshal(); err == nil {
		t.Error("expected error for MIME type > 255 bytes")
	}
}

func TestLatencyPingPongRoundTrip(t *testing.T) {
	ping := &LatencyPingPayload{Timestamp: 1234567890}
	data := ping.Marshal()
	got, err := UnmarshalLatencyPing(data)
	if err != nil {
		t.Fatal(err)
	}
	if got.Timestamp != ping.Timestamp {
		t.Errorf("Timestamp: got %d, want %d", got.Timestamp, ping.Timestamp)
	}

	pong := &LatencyPongPayload{Timestamp: ping.Timestamp}
	data = pong.Marshal()
	gotPong, err := UnmarshalLatencyPong(data)
	if err != nil {
		t.Fatal(err)
	}
	if gotPong.Timestamp != pong.Timestamp {
		t.Errorf("Timestamp: got %d, want %d", gotPong.Timestamp, pong.Timestamp)
	}
}

func TestDisconnectRoundTrip(t *testing.T) {
	orig := &DisconnectPayload{Reason: DisconnectUserInitiated}
	data := orig.Marshal()
	got, err := UnmarshalDisconnect(data)
	if err != nil {
		t.Fatal(err)
	}
	if got.Reason != orig.Reason {
		t.Errorf("Reason: got 0x%02X, want 0x%02X", got.Reason, orig.Reason)
	}
}

func TestErrorPayloadRoundTrip(t *testing.T) {
	orig := &ErrorPayload{Code: ErrHmacMismatch, Message: "hmac verification failed"}
	data, err := orig.Marshal()
	if err != nil {
		t.Fatal(err)
	}
	got, err := UnmarshalError(data)
	if err != nil {
		t.Fatal(err)
	}
	if got.Code != orig.Code {
		t.Errorf("Code: got 0x%02X, want 0x%02X", got.Code, orig.Code)
	}
	if got.Message != orig.Message {
		t.Errorf("Message: got %q, want %q", got.Message, orig.Message)
	}
}

func TestErrorMessageTooLong(t *testing.T) {
	longMsg := make([]byte, 254) // MaxErrorMessageLen = 253
	p := &ErrorPayload{Code: ErrHmacMismatch, Message: string(longMsg)}
	if _, err := p.Marshal(); err == nil {
		t.Error("expected error for message > 253 bytes")
	}
}

func TestPayloadTooShort(t *testing.T) {
	if _, err := UnmarshalKeyDown([]byte{0x01}); err == nil {
		t.Error("expected error for short KEY_DOWN")
	}
	if _, err := UnmarshalMouseMove([]byte{0x01, 0x02}); err == nil {
		t.Error("expected error for short MOUSE_MOVE")
	}
	if _, err := UnmarshalClipboard([]byte{0x03}); err == nil {
		t.Error("expected error for short CLIPBOARD")
	}
	if _, err := UnmarshalError([]byte{0x01}); err == nil {
		t.Error("expected error for short ERROR")
	}
}
