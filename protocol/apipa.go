package protocol

import (
	"crypto/rand"
	"fmt"
	"net"
)

// APIPA address negotiation for direct Ethernet links (§3.2.2).
//
//	Host:   169.254.{R}.1/30
//	Target: 169.254.{R}.2/30   with random R ∈ [1, 254]
//
// Collision handling (ARP probing) is platform-specific; this file covers the
// platform-independent generation, reconstruction from a persisted R, and
// validation. Peers persist R after a successful link so the next connection
// reuses the subnet with zero discovery.

const (
	APIPAPrefix    = "169.254."
	APIPAMaskSize  = 30
	APIPAMaskValue = "255.255.255.252" // /30
)

// APIPASubnet holds the negotiated link-local addresses for both roles.
type APIPASubnet struct {
	R        byte   // third octet, 1-254
	HostIP   net.IP // 169.254.R.1
	TargetIP net.IP // 169.254.R.2
	Mask     net.IPMask
}

// GenerateAPIPASubnet picks a random R and returns the matching /30 subnet.
// Caller must still ARP-probe for conflicts (§3.2.2) and retry on collision.
func GenerateAPIPASubnet() (*APIPASubnet, error) {
	var b [1]byte
	if _, err := rand.Read(b[:]); err != nil {
		return nil, fmt.Errorf("read randomness: %w", err)
	}
	r := b[0]
	if r == 0 || r == 255 {
		r = 1
	}
	return APIPASubnetFromR(r), nil
}

// APIPASubnetFromR rebuilds the subnet from a persisted R value, enabling the
// 0-second reconnect path (§3.2.2 持久化).
func APIPASubnetFromR(r byte) *APIPASubnet {
	if r == 0 || r == 255 {
		r = 1
	}
	return &APIPASubnet{
		R:        r,
		HostIP:   net.ParseIP(fmt.Sprintf("%s%d.1", APIPAPrefix, r)).To4(),
		TargetIP: net.ParseIP(fmt.Sprintf("%s%d.2", APIPAPrefix, r)).To4(),
		Mask:     net.CIDRMask(APIPAMaskSize, 32),
	}
}

// Validate checks that both IPs are the host/target pair of one legal
// 169.254.R.0/30 subnet.
func (s *APIPASubnet) Validate() error {
	if s.R == 0 || s.R == 255 {
		return fmt.Errorf("APIPA R out of range: %d", s.R)
	}
	if !s.HostIP.Equal(net.ParseIP(fmt.Sprintf("%s%d.1", APIPAPrefix, s.R))) ||
		!s.TargetIP.Equal(net.ParseIP(fmt.Sprintf("%s%d.2", APIPAPrefix, s.R))) {
		return fmt.Errorf("IPs do not match 169.254.%d.1/2", s.R)
	}
	return nil
}

// ValidateAPIPAPair reports whether two addresses belong to the same legal
// APIPA /30 subnet as host and target (either role order).
func ValidateAPIPAPair(a, b net.IP) bool {
	a4, b4 := a.To4(), b.To4()
	if a4 == nil || b4 == nil {
		return false
	}
	for r := 1; r <= 254; r++ {
		s := APIPASubnetFromR(byte(r))
		if (s.HostIP.Equal(a4) && s.TargetIP.Equal(b4)) ||
			(s.HostIP.Equal(b4) && s.TargetIP.Equal(a4)) {
			return true
		}
	}
	return false
}
