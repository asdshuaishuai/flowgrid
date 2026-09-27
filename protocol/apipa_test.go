package protocol

import (
	"net"
	"testing"
)

func TestAPIPASubnetFromR(t *testing.T) {
	s := APIPASubnetFromR(42)
	if s.HostIP.String() != "169.254.42.1" {
		t.Errorf("host IP: got %s, want 169.254.42.1", s.HostIP)
	}
	if s.TargetIP.String() != "169.254.42.2" {
		t.Errorf("target IP: got %s, want 169.254.42.2", s.TargetIP)
	}
	if s.Mask.String() != "/fffffffC" && net.IPv4Mask(255, 255, 255, 252).String() != s.Mask.String() {
		t.Errorf("mask: got %s, want 255.255.255.252", s.Mask)
	}
	if err := s.Validate(); err != nil {
		t.Errorf("Validate: %v", err)
	}
}

func TestAPIPASubnetFromRClampsIllegal(t *testing.T) {
	for _, r := range []byte{0, 255} {
		s := APIPASubnetFromR(r)
		if s.R == 0 || s.R == 255 {
			t.Errorf("R %d not clamped", r)
		}
	}
}

func TestGenerateAPIPASubnet(t *testing.T) {
	seen := map[byte]bool{}
	for i := 0; i < 50; i++ {
		s, err := GenerateAPIPASubnet()
		if err != nil {
			t.Fatalf("GenerateAPIPASubnet: %v", err)
		}
		if s.R == 0 || s.R == 255 {
			t.Fatalf("R out of range: %d", s.R)
		}
		if err := s.Validate(); err != nil {
			t.Fatalf("Validate: %v", err)
		}
		seen[s.R] = true
	}
	if len(seen) < 5 {
		t.Errorf("generation looks constant: only %d distinct R in 50 tries", len(seen))
	}
}

func TestValidateAPIPAPair(t *testing.T) {
	s := APIPASubnetFromR(7)
	if !ValidateAPIPAPair(s.HostIP, s.TargetIP) {
		t.Error("host/target pair must validate")
	}
	if !ValidateAPIPAPair(s.TargetIP, s.HostIP) {
		t.Error("pair validation must accept either role order")
	}
	if ValidateAPIPAPair(s.HostIP, net.ParseIP("169.254.8.2")) {
		t.Error("cross-subnet pair must not validate")
	}
	if ValidateAPIPAPair(s.HostIP, net.ParseIP("169.254.7.3")) {
		t.Error(".3 is the broadcast address of the /30 and must not validate")
	}
	if ValidateAPIPAPair(net.ParseIP("192.168.1.1"), net.ParseIP("192.168.1.2")) {
		t.Error("non-link-local pair must not validate")
	}
	if ValidateAPIPAPair(nil, s.TargetIP) {
		t.Error("nil/invalid IP must not validate")
	}
}
