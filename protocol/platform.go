package protocol

// Platform identifies the operating system (§3.1.2).
type Platform uint8

const (
	PlatformMacOS   Platform = 0x01
	PlatformWindows Platform = 0x02
	PlatformLinux   Platform = 0x03
	PlatformAndroid Platform = 0x04
	PlatformIOS     Platform = 0x05
)

// String returns the platform name.
func (p Platform) String() string {
	switch p {
	case PlatformMacOS:
		return "macOS"
	case PlatformWindows:
		return "Windows"
	case PlatformLinux:
		return "Linux"
	case PlatformAndroid:
		return "Android"
	case PlatformIOS:
		return "iOS/iPadOS"
	default:
		return "Unknown"
	}
}

// Role identifies the device role in a connection (§4.2).
type Role uint8

const (
	RoleHost   Role = 0x01 // keyboard/mouse sender
	RoleTarget Role = 0x02 // keyboard/mouse receiver
	RoleBoth   Role = 0x03 // bidirectional
)

// CapabilityFlags bit masks (§3.1.2).
type CapabilityFlags uint8

const (
	CapNearLink     CapabilityFlags = 0x01
	CapDirectLink   CapabilityFlags = 0x02
	CapWiFiDirect   CapabilityFlags = 0x04
	CapDTLS13       CapabilityFlags = 0x08
	CapClipboard    CapabilityFlags = 0x10
	CapFileTransfer CapabilityFlags = 0x20
)

// Has checks if flag has a specific capability set.
func (f CapabilityFlags) Has(cap CapabilityFlags) bool {
	return f&cap != 0
}
