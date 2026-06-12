package protocol

import (
	"testing"
)

func TestWiFiDirectRequestRoundTrip(t *testing.T) {
	orig := &WiFiDirectRequest{
		Version:  1,
		Port:     24801,
		IPAddr:   [4]byte{192, 168, 1, 100},
		Subnet:   [4]byte{255, 255, 255, 0},
		Channel:  36,
		Band:     WiFiBand5GHz,
		Security: WiFiSecurityWPA3,
		PSKHash:  [4]byte{0xAB, 0xCD, 0xEF, 0x01},
	}
	data := orig.Marshal()
	if len(data) != 20 {
		t.Fatalf("got %d bytes, want 20", len(data))
	}

	got, err := UnmarshalWiFiDirectRequest(data)
	if err != nil {
		t.Fatal(err)
	}
	if got.Version != orig.Version {
		t.Errorf("Version: got %d, want %d", got.Version, orig.Version)
	}
	if got.Port != orig.Port {
		t.Errorf("Port: got %d, want %d", got.Port, orig.Port)
	}
	if got.IPAddr != orig.IPAddr {
		t.Errorf("IPAddr: got %v, want %v", got.IPAddr, orig.IPAddr)
	}
	if got.Channel != orig.Channel {
		t.Errorf("Channel: got %d, want %d", got.Channel, orig.Channel)
	}
	if got.Band != orig.Band {
		t.Errorf("Band: got %d, want %d", got.Band, orig.Band)
	}
	if got.Security != orig.Security {
		t.Errorf("Security: got %d, want %d", got.Security, orig.Security)
	}
	if got.PSKHash != orig.PSKHash {
		t.Errorf("PSKHash: got %v, want %v", got.PSKHash, orig.PSKHash)
	}
}

func TestWiFiDirectResponseRoundTrip(t *testing.T) {
	orig := &WiFiDirectResponse{
		Version: 1,
		Status:  WiFiDirectAccept,
		Port:    24801,
		Channel: 36,
		Band:    WiFiBand5GHz,
	}
	data := orig.Marshal()
	if len(data) != 8 {
		t.Fatalf("got %d bytes, want 8", len(data))
	}

	got, err := UnmarshalWiFiDirectResponse(data)
	if err != nil {
		t.Fatal(err)
	}
	if got.Version != orig.Version {
		t.Errorf("Version: got %d, want %d", got.Version, orig.Version)
	}
	if got.Status != orig.Status {
		t.Errorf("Status: got %d, want %d", got.Status, orig.Status)
	}
	if got.Port != orig.Port {
		t.Errorf("Port: got %d, want %d", got.Port, orig.Port)
	}
	if got.Channel != orig.Channel {
		t.Errorf("Channel: got %d, want %d", got.Channel, orig.Channel)
	}
	if got.Band != orig.Band {
		t.Errorf("Band: got %d, want %d", got.Band, orig.Band)
	}
}

func TestWiFiDirectResponseReject(t *testing.T) {
	resp := &WiFiDirectResponse{
		Version: 1,
		Status:  WiFiDirectRejectBusy,
		Port:    0,
		Channel: 0,
		Band:    0,
	}
	data := resp.Marshal()
	got, err := UnmarshalWiFiDirectResponse(data)
	if err != nil {
		t.Fatal(err)
	}
	if got.Status != WiFiDirectRejectBusy {
		t.Errorf("Status: got %d, want %d", got.Status, WiFiDirectRejectBusy)
	}
}

func TestWiFiDirectRequestShortData(t *testing.T) {
	_, err := UnmarshalWiFiDirectRequest(make([]byte, 10))
	if err == nil {
		t.Fatal("expected error for short request")
	}
}

func TestWiFiDirectResponseShortData(t *testing.T) {
	_, err := UnmarshalWiFiDirectResponse(make([]byte, 4))
	if err == nil {
		t.Fatal("expected error for short response")
	}
}
