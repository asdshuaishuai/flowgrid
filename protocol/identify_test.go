package protocol

import (
	"testing"
)

func TestIdentifyRoundTrip(t *testing.T) {
	orig := &IdentifyPayload{
		Version:  1,
		Platform: PlatformMacOS,
		Role:     RoleHost,
		Caps:     CapIntelligentLink | CapDTLS13 | CapClipboard,
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
		Caps:     CapIntelligentLink | CapDirectLink,
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

// §2.2 requires reserved fields to be zero on the wire, and §3.1.2 reserves
// capability bit 6-7. A caller that sets those bits must not leak them onto
// the wire — the marshal paths mask them off.
func TestIdentifyCapsReservedBitsCleared(t *testing.T) {
	const reserved = CapabilityFlags(0xC0)

	id := &IdentifyPayload{
		Version:  1,
		Platform: PlatformMacOS,
		Role:     RoleHost,
		Caps:     CapIntelligentLink | reserved,
		SeqInit:  1,
		Name:     "x",
	}
	if got := CapabilityFlags(id.Marshal()[3]); got&reserved != 0 {
		t.Errorf("IDENTIFY leaked reserved caps bits: 0x%02X", got)
	}

	resp := &IdentifyResponsePayload{
		Status:   IdentifyAccepted,
		Platform: PlatformMacOS,
		Caps:     CapDirectLink | reserved,
		Name:     "x",
	}
	if got := CapabilityFlags(resp.Marshal()[2]); got&reserved != 0 {
		t.Errorf("IDENTIFY_RESPONSE leaked reserved caps bits: 0x%02X", got)
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

// IDENTIFY is the version-negotiation checkpoint: a foreign version or a
// out-of-range enum must fail the decode with the proper protocol error.
func TestIdentifyInvalidFieldsRejected(t *testing.T) {
	good := (&IdentifyPayload{
		Version: ProtocolVersion, Platform: PlatformLinux, Role: RoleBoth, SeqInit: 1, Name: "x",
	}).Marshal()

	badVersion := append([]byte(nil), good...)
	badVersion[0] = 99
	if _, err := UnmarshalIdentify(badVersion); err == nil {
		t.Error("foreign version must be rejected")
	} else if pe, ok := err.(*ProtocolError); !ok || pe.Code != ErrVersionMismatch {
		t.Errorf("got %v, want ErrVersionMismatch", err)
	}

	badRole := append([]byte(nil), good...)
	badRole[2] = 0x7F
	if _, err := UnmarshalIdentify(badRole); err == nil {
		t.Error("unknown role must be rejected")
	} else if pe, ok := err.(*ProtocolError); !ok || pe.Code != ErrInvalidPayload {
		t.Errorf("got %v, want ErrInvalidPayload", err)
	}

	badPlatform := append([]byte(nil), good...)
	badPlatform[1] = 0x06
	if _, err := UnmarshalIdentify(badPlatform); err == nil {
		t.Error("unknown platform must be rejected")
	}

	badRespPlatform := []byte{IdentifyAccepted, 0x00, 0x01, 'n', 0}
	if _, err := UnmarshalIdentifyResponse(badRespPlatform); err == nil {
		t.Error("unknown response platform must be rejected")
	}
}
