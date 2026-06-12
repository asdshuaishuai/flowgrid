package protocol

import (
	"encoding/binary"
	"errors"
	"fmt"
	"strings"
)

// BLE Service UUID: 00001850-0000-1000-8000-00805F9B34FB (§3.1.1)
const BLEServiceUUID16 uint16 = 0x1850

// BLE Characteristics (§3.1.1)
const (
	CharHIDWrite   uint16 = 0x1851 // TX: write HID frame
	CharHIDNotify  uint16 = 0x1852 // RX: notify HID frame
	CharWiFiDirect uint16 = 0x1853 // WiFi Direct params
	CharDeviceInfo  uint16 = 0x1854 // device name/platform
)

// BLEAdvertisingData represents the 7-byte BLE advertising packet (§3.1.2).
type BLEAdvertisingData struct {
	ProtocolVersion uint8
	DevicePlatform  Platform
	CapabilityFlags CapabilityFlags
	WiFiDirectChan  uint16
}

// MarshalBLEAdvertising encodes the BLE advertising data (7 bytes).
func (d *BLEAdvertisingData) MarshalBLEAdvertising() []byte {
	buf := make([]byte, 7)
	binary.BigEndian.PutUint16(buf[0:2], BLEServiceUUID16)
	buf[2] = d.ProtocolVersion
	buf[3] = uint8(d.DevicePlatform)
	buf[4] = uint8(d.CapabilityFlags)
	binary.BigEndian.PutUint16(buf[5:7], d.WiFiDirectChan)
	return buf
}

// UnmarshalBLEAdvertising decodes a 7-byte BLE advertising packet.
func UnmarshalBLEAdvertising(data []byte) (*BLEAdvertisingData, error) {
	if len(data) < 7 {
		return nil, errors.New("BLE advertising data too short")
	}
	uuid := binary.BigEndian.Uint16(data[0:2])
	if uuid != BLEServiceUUID16 {
		return nil, fmt.Errorf("unexpected service UUID: 0x%04X", uuid)
	}
	return &BLEAdvertisingData{
		ProtocolVersion: data[2],
		DevicePlatform:  Platform(data[3]),
		CapabilityFlags: CapabilityFlags(data[4]),
		WiFiDirectChan:  binary.BigEndian.Uint16(data[5:7]),
	}, nil
}

// MdnsServiceType is the mDNS service type for DirectLink (§3.2.1).
const MdnsServiceType = "_flowgrid._tcp"

// MdnsTXTRecord holds the mDNS TXT record fields (§3.2.1).
type MdnsTXTRecord struct {
	Version    string // "1"
	Platform   string // "macos" / "windows" / "linux"
	Name       string // device name
	Caps       string // comma-separated: "nearlink,directlink,dtls,clipboard"
	Port       string // UDP data port, e.g. "24801"
	Encryption string // "dtls13" / "none"
	HWAddr     string // MAC address "a1:b2:c3:d4:e5:f6"
}

// MarshalTXT encodes the TXT record as key=value pairs.
func (r *MdnsTXTRecord) MarshalTXT() []string {
	return []string{
		"version=" + r.Version,
		"platform=" + r.Platform,
		"name=" + r.Name,
		"caps=" + r.Caps,
		"port=" + r.Port,
		"encryption=" + r.Encryption,
		"hwaddr=" + r.HWAddr,
	}
}

// UnmarshalTXT parses key=value TXT record lines.
func UnmarshalTXT(lines []string) *MdnsTXTRecord {
	r := &MdnsTXTRecord{}
	for _, line := range lines {
		parts := strings.SplitN(line, "=", 2)
		if len(parts) != 2 {
			continue
		}
		switch parts[0] {
		case "version":
			r.Version = parts[1]
		case "platform":
			r.Platform = parts[1]
		case "name":
			r.Name = parts[1]
		case "caps":
			r.Caps = parts[1]
		case "port":
			r.Port = parts[1]
		case "encryption":
			r.Encryption = parts[1]
		case "hwaddr":
			r.HWAddr = parts[1]
		}
	}
	return r
}

// RSSI quality levels (§3.1.5).
type SignalQuality int

const (
	SignalWeak       SignalQuality = 1 // RSSI <= -80 dBm
	SignalFair       SignalQuality = 2 // RSSI > -80 dBm
	SignalGood       SignalQuality = 3 // RSSI > -65 dBm
	SignalExcellent  SignalQuality = 4 // RSSI > -50 dBm
)

// ClassifyRSSI returns the signal quality for an RSSI value in dBm.
func ClassifyRSSI(rssi int) SignalQuality {
	switch {
	case rssi > -50:
		return SignalExcellent
	case rssi > -65:
		return SignalGood
	case rssi > -80:
		return SignalFair
	default:
		return SignalWeak
	}
}
