package protocol

import (
	"testing"
)

func TestIdentifyRoundTrip(t *testing.T) {
	orig := &IdentifyPayload{
		Version:  1,
		Platform: PlatformMacOS,
		Role:     RoleHost,
		Caps:     CapNearLink | CapDTLS13 | CapClipboard,
		SeqInit:  12345,
		Name:     "MacBook Pro",
	}
	data := orig.Marshal()
	got, err := UnmarshalIdentify(data)
	if err != nil {
		t.Fatal(err)
	}
	if got.Version != orig.Version {
		t.Errorf("Version: got %d, want %d", got.Version, orig.Version)
	}
	if got.Platform != orig.Platform {
		t.Errorf("Platform: got %v, want %v", got.Platform, orig.Platform)
	}
	if got.Role != orig.Role {
		t.Errorf("Role: got %d, want %d", got.Role, orig.Role)
	}
	if got.Caps != orig.Caps {
		t.Errorf("Caps: got 0x%02X, want 0x%02X", got.Caps, orig.Caps)
	}
	if got.SeqInit != orig.SeqInit {
		t.Errorf("SeqInit: got %d, want %d", got.SeqInit, orig.SeqInit)
	}
	if got.Name != orig.Name {
		t.Errorf("Name: got %q, want %q", got.Name, orig.Name)
	}
}

func TestIdentifyResponseRoundTrip(t *testing.T) {
	orig := &IdentifyResponsePayload{
		Status:   IdentifyAccepted,
		Platform: PlatformWindows,
		Caps:     CapNearLink | CapDirectLink,
		Name:     "Windows Desktop",
	}
	data := orig.Marshal()
	got, err := UnmarshalIdentifyResponse(data)
	if err != nil {
		t.Fatal(err)
	}
	if got.Status != orig.Status {
		t.Errorf("Status: got %d, want %d", got.Status, orig.Status)
	}
	if got.Platform != orig.Platform {
		t.Errorf("Platform: got %v, want %v", got.Platform, orig.Platform)
	}
	if got.Name != orig.Name {
		t.Errorf("Name: got %q, want %q", got.Name, orig.Name)
	}
}

func TestIdentifyEmptyName(t *testing.T) {
	orig := &IdentifyPayload{
		Version:  1,
		Platform: PlatformLinux,
		Role:     RoleTarget,
		SeqInit:  0,
		Name:     "",
	}
	data := orig.Marshal()
	got, err := UnmarshalIdentify(data)
	if err != nil {
		t.Fatal(err)
	}
	if got.Name != "" {
		t.Errorf("Name: got %q, want empty", got.Name)
	}
}

func TestIdentifyTooShort(t *testing.T) {
	if _, err := UnmarshalIdentify([]byte{0x01, 0x02}); err == nil {
		t.Error("expected error for short IDENTIFY")
	}
	if _, err := UnmarshalIdentifyResponse([]byte{0x01}); err == nil {
		t.Error("expected error for short IDENTIFY_RESPONSE")
	}
}
