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
	// CapIntelligentLink（bit 0）：支持智能连接（LAN 私有链发现，§3.1）。
	CapIntelligentLink CapabilityFlags = 0x01
	// CapNearLink 是 v1.1 之前的旧名，指向同一位；新代码请用
	// CapIntelligentLink。保留别名只为不打断既有调用点与线格式。
	CapNearLink     CapabilityFlags = CapIntelligentLink
	CapDirectLink   CapabilityFlags = 0x02
	CapWiFiDirect   CapabilityFlags = 0x04
	CapDTLS13       CapabilityFlags = 0x08
	CapClipboard    CapabilityFlags = 0x10
	CapFileTransfer CapabilityFlags = 0x20

	// CapDefinedMask is the set of bits this version assigns. §2.2 requires
	// reserved fields to be zero on the wire and §3.1.2 reserves bit 6-7, so
	// the marshal paths mask Caps with this before writing. Kept as a named
	// constant — and derived from the Cap* values above — so that defining a
	// new capability bit here widens the mask automatically instead of the
	// old hardcoded 0x3F silently clearing it.
	CapDefinedMask CapabilityFlags = CapIntelligentLink | CapDirectLink | CapWiFiDirect |
		CapDTLS13 | CapClipboard | CapFileTransfer
)

// Has checks if flag has a specific capability set.
func (f CapabilityFlags) Has(cap CapabilityFlags) bool {
	return f&cap != 0
}
