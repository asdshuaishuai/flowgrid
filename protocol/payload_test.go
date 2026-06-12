package protocol

import (
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
}

func TestClipboardMIMETooLong(t *testing.T) {
	longMIME := make([]byte, 256)
	for i := range longMIME {
		longMIME[i] = 'a'
	}
	p := &ClipboardPayload{MIMEType: string(longMIME), Data: []byte("x")}
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
