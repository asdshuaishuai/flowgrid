package protocol

import (
	"encoding/binary"
	"errors"
)

// WiFi band constants (§3.1.3)
const (
	WiFiBand24GHz uint8 = 0x01
	WiFiBand5GHz  uint8 = 0x02
)

// WiFi security constants (§3.1.3)
const (
	WiFiSecurityWPA2 uint8 = 0x01
	WiFiSecurityWPA3 uint8 = 0x02
)

// WiFi Direct parameter exchange status codes (§3.1.3)
const (
	WiFiDirectAccept     uint8 = 0x00
	WiFiDirectRejectBusy uint8 = 0x01
	WiFiDirectRejectIncompat uint8 = 0x02
	WiFiDirectError      uint8 = 0xFF
)

// WiFiDirectRequest is the 20-byte BLE characteristic exchange (§3.1.3).
type WiFiDirectRequest struct {
	Version  uint8   // 0x01
	Port     uint16  // data port
	IPAddr   [4]byte // IPv4 address
	Subnet   [4]byte // subnet mask
	Channel  uint8   // WiFi channel
	Band     uint8   // 0x01=2.4GHz, 0x02=5GHz
	Security uint8   // 0x01=WPA2, 0x02=WPA3
	PSKHash  [4]byte // first 4 bytes of SHA-256(PSK)
}

const wifiDirectReqSize = 20

// Marshal encodes the WiFi Direct request to 20 bytes.
func (r *WiFiDirectRequest) Marshal() []byte {
	buf := make([]byte, wifiDirectReqSize)
	buf[0] = r.Version
	binary.BigEndian.PutUint16(buf[1:3], r.Port)
	copy(buf[3:7], r.IPAddr[:])
	copy(buf[7:11], r.Subnet[:])
	buf[11] = r.Channel
	buf[12] = r.Band
	buf[13] = r.Security
	// buf[14] reserved (0x00)
	copy(buf[15:19], r.PSKHash[:])
	// buf[19] reserved (0x00)
	return buf
}

// UnmarshalWiFiDirectRequest decodes a 20-byte WiFi Direct request.
func UnmarshalWiFiDirectRequest(data []byte) (*WiFiDirectRequest, error) {
	if len(data) < wifiDirectReqSize {
		return nil, errors.New("WiFi Direct request too short")
	}
	r := &WiFiDirectRequest{
		Version:  data[0],
		Port:     binary.BigEndian.Uint16(data[1:3]),
		Channel:  data[11],
		Band:     data[12],
		Security: data[13],
	}
	copy(r.IPAddr[:], data[3:7])
	copy(r.Subnet[:], data[7:11])
	copy(r.PSKHash[:], data[15:19])
	return r, nil
}

// WiFiDirectResponse is the 8-byte response (§3.1.3).
type WiFiDirectResponse struct {
	Version uint8
	Status  uint8
	Port    uint16
	Channel uint16
	Band    uint8
}

const wifiDirectRespSize = 8

// Marshal encodes the WiFi Direct response to 8 bytes.
func (r *WiFiDirectResponse) Marshal() []byte {
	buf := make([]byte, wifiDirectRespSize)
	buf[0] = r.Version
	buf[1] = r.Status
	binary.BigEndian.PutUint16(buf[2:4], r.Port)
	binary.BigEndian.PutUint16(buf[4:6], r.Channel)
	buf[6] = r.Band
	// buf[7] reserved (0x00)
	return buf
}

// UnmarshalWiFiDirectResponse decodes an 8-byte WiFi Direct response.
func UnmarshalWiFiDirectResponse(data []byte) (*WiFiDirectResponse, error) {
	if len(data) < wifiDirectRespSize {
		return nil, errors.New("WiFi Direct response too short")
	}
	return &WiFiDirectResponse{
		Version: data[0],
		Status:  data[1],
		Port:    binary.BigEndian.Uint16(data[2:4]),
		Channel: binary.BigEndian.Uint16(data[4:6]),
		Band:    data[6],
	}, nil
}

// preferredWiFiChannels lists recommended channels to avoid Apple AWDL interference (§3.1.4).
var preferredChannels24GHz = []uint8{1, 11}
var preferredChannels5GHz = []uint8{36, 40, 48, 153, 157, 161}

// avoidedChannels lists channels occupied by Apple AWDL (§3.1.4).
var avoidedChannels24GHz = []uint8{6}
var avoidedChannels5GHz = []uint8{44, 149}

// PreferredWiFiChannels returns copies of the recommended WiFi channels.
func PreferredWiFiChannels() (channels24GHz, channels5GHz []uint8) {
	c24 := make([]uint8, len(preferredChannels24GHz))
	copy(c24, preferredChannels24GHz)
	c5 := make([]uint8, len(preferredChannels5GHz))
	copy(c5, preferredChannels5GHz)
	return c24, c5
}

// AvoidedWiFiChannels returns copies of the WiFi channels to avoid.
func AvoidedWiFiChannels() (channels24GHz, channels5GHz []uint8) {
	c24 := make([]uint8, len(avoidedChannels24GHz))
	copy(c24, avoidedChannels24GHz)
	c5 := make([]uint8, len(avoidedChannels5GHz))
	copy(c5, avoidedChannels5GHz)
	return c24, c5
}
