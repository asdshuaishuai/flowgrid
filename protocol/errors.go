package protocol

import "fmt"

// ErrorCode represents a FlowGrid protocol error code.
type ErrorCode uint8

// Transport layer errors (§12.1)
const (
	ErrPeripheralNotFound    ErrorCode = 0x01
	ErrConnectionFailed      ErrorCode = 0x02
	ErrConnectionCancelled   ErrorCode = 0x03
	ErrConnectionTimeout     ErrorCode = 0x04
	ErrNotConnected          ErrorCode = 0x05
	ErrSendFailed            ErrorCode = 0x06
	ErrDTLSError             ErrorCode = 0x07
	ErrBluetoothNotAvail     ErrorCode = 0x08
	ErrBluetoothPoweredOff   ErrorCode = 0x09
	ErrBluetoothUnauthorized ErrorCode = 0x0A
	ErrBluetoothNotSupported ErrorCode = 0x0B
	ErrWiFiDirectFailed      ErrorCode = 0x0C
	ErrMdnsError             ErrorCode = 0x0D
	ErrNetworkUnavailable    ErrorCode = 0x0E
)

// Protocol layer errors (§12.2)
const (
	ErrHmacMismatch         ErrorCode = 0x10
	ErrUnknownFrameType     ErrorCode = 0x11
	ErrInvalidPayload       ErrorCode = 0x12
	ErrVersionMismatch      ErrorCode = 0x13
	ErrIdentifyRejected     ErrorCode = 0x14
	ErrFragmentationTimeout ErrorCode = 0x15
	ErrSequenceGap          ErrorCode = 0x20
)

// HAL errors (§12.3)
const (
	ErrAccessibilityDenied    ErrorCode = 0x30
	ErrEventCreationFailed    ErrorCode = 0x31
	ErrUnsupportedEventType   ErrorCode = 0x32
	ErrPlatformNotSupported   ErrorCode = 0x33
	ErrUinputPermissionDenied ErrorCode = 0x34
)

// Disconnect reasons (§12.4)
const (
	DisconnectUserInitiated uint8 = 0x00
	DisconnectShutdown      uint8 = 0x01
	DisconnectSwitchDevice  uint8 = 0x02
	DisconnectProtocolError uint8 = 0x03
	DisconnectSecurityError uint8 = 0x04
	DisconnectUnknown       uint8 = 0xFF
)

// ProtocolError is a FlowGrid protocol error with code and message.
type ProtocolError struct {
	Code    ErrorCode
	Message string
}

func (e *ProtocolError) Error() string {
	return fmt.Sprintf("flowgrid error 0x%02X: %s", e.Code, e.Message)
}

// NewError creates a new ProtocolError.
func NewError(code ErrorCode, msg string) *ProtocolError {
	return &ProtocolError{Code: code, Message: msg}
}

// errorCodeNames maps every §12 code to its canonical spec name.
var errorCodeNames = map[ErrorCode]string{
	// §12.1 transport
	ErrPeripheralNotFound:    "peripheralNotFound",
	ErrConnectionFailed:      "connectionFailed",
	ErrConnectionCancelled:   "connectionCancelled",
	ErrConnectionTimeout:     "connectionTimeout",
	ErrNotConnected:          "notConnected",
	ErrSendFailed:            "sendFailed",
	ErrDTLSError:             "dtlsError",
	ErrBluetoothNotAvail:     "bluetoothNotAvailable",
	ErrBluetoothPoweredOff:   "bluetoothPoweredOff",
	ErrBluetoothUnauthorized: "bluetoothUnauthorized",
	ErrBluetoothNotSupported: "bluetoothNotSupported",
	ErrWiFiDirectFailed:      "wifiDirectFailed",
	ErrMdnsError:             "mdnsError",
	ErrNetworkUnavailable:    "networkUnavailable",
	// §12.2 protocol
	ErrHmacMismatch:         "hmacMismatch",
	ErrUnknownFrameType:     "unknownFrameType",
	ErrInvalidPayload:       "invalidPayload",
	ErrVersionMismatch:      "versionMismatch",
	ErrIdentifyRejected:     "identifyRejected",
	ErrFragmentationTimeout: "fragmentationTimeout",
	ErrSequenceGap:          "sequenceGap",
	// §12.3 HAL
	ErrAccessibilityDenied:    "accessibilityDenied",
	ErrEventCreationFailed:    "eventCreationFailed",
	ErrUnsupportedEventType:   "unsupportedEventType",
	ErrPlatformNotSupported:   "platformNotSupported",
	ErrUinputPermissionDenied: "uinputPermissionDenied",
}

// ErrorCodeString returns the canonical §12 name for a code, or a generic
// "error 0x%02X" string for unknown codes (future versions may add codes).
func ErrorCodeString(code ErrorCode) string {
	if name, ok := errorCodeNames[code]; ok {
		return name
	}
	return fmt.Sprintf("error 0x%02X", uint8(code))
}
