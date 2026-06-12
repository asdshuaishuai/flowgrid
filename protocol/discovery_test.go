package protocol

import (
	"testing"
)

func TestBLEAdvertisingRoundTrip(t *testing.T) {
	orig := &BLEAdvertisingData{
		ProtocolVersion: 1,
		DevicePlatform:  PlatformMacOS,
		CapabilityFlags: CapNearLink | CapWiFiDirect | CapDTLS13,
		WiFiDirectChan:  36,
	}
	data := orig.MarshalBLEAdvertising()
	if len(data) != 7 {
		t.Fatalf("BLE advertising data: got %d bytes, want 7", len(data))
	}

	got, err := UnmarshalBLEAdvertising(data)
	if err != nil {
		t.Fatal(err)
	}
	if got.ProtocolVersion != orig.ProtocolVersion {
		t.Errorf("ProtocolVersion: got %d, want %d", got.ProtocolVersion, orig.ProtocolVersion)
	}
	if got.DevicePlatform != orig.DevicePlatform {
		t.Errorf("Platform: got %v, want %v", got.DevicePlatform, orig.DevicePlatform)
	}
	if got.CapabilityFlags != orig.CapabilityFlags {
		t.Errorf("Caps: got 0x%02X, want 0x%02X", got.CapabilityFlags, orig.CapabilityFlags)
	}
	if got.WiFiDirectChan != orig.WiFiDirectChan {
		t.Errorf("Channel: got %d, want %d", got.WiFiDirectChan, orig.WiFiDirectChan)
	}
}

func TestBLEAdvertisingBadUUID(t *testing.T) {
	data := []byte{0x00, 0x00, 0x01, 0x01, 0x01, 0x00, 0x24}
	if _, err := UnmarshalBLEAdvertising(data); err == nil {
		t.Error("expected error for bad UUID")
	}
}

func TestMdnsTXTRoundTrip(t *testing.T) {
	orig := &MdnsTXTRecord{
		Version:    "1",
		Platform:   "macos",
		Name:       "MacBook Pro",
		Caps:       "nearlink,directlink,dtls,clipboard",
		Port:       "24801",
		Encryption: "dtls13",
		HWAddr:     "a1:b2:c3:d4:e5:f6",
	}
	lines := orig.MarshalTXT()
	got := UnmarshalTXT(lines)

	if got.Version != orig.Version {
		t.Errorf("Version: got %q, want %q", got.Version, orig.Version)
	}
	if got.Platform != orig.Platform {
		t.Errorf("Platform: got %q, want %q", got.Platform, orig.Platform)
	}
	if got.Name != orig.Name {
		t.Errorf("Name: got %q, want %q", got.Name, orig.Name)
	}
	if got.Port != orig.Port {
		t.Errorf("Port: got %q, want %q", got.Port, orig.Port)
	}
	if got.Encryption != orig.Encryption {
		t.Errorf("Encryption: got %q, want %q", got.Encryption, orig.Encryption)
	}
}

func TestClassifyRSSI(t *testing.T) {
	tests := []struct {
		rssi int
		want SignalQuality
	}{
		{-30, SignalExcellent},
		{-50, SignalGood},
		{-65, SignalFair},
		{-80, SignalWeak},
		{-90, SignalWeak},
	}
	for _, tt := range tests {
		got := ClassifyRSSI(tt.rssi)
		if got != tt.want {
			t.Errorf("ClassifyRSSI(%d): got %d, want %d", tt.rssi, got, tt.want)
		}
	}
}
