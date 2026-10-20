import Foundation

/// cgo 会话接口返回的 §12 协议错误码（如 0x05 notConnected / 0x06 sendFailed /
/// 0x15 transferTimeout）。文案经 `fg_error_string` 获取。
public struct GoProtocolError: Error, Equatable, Sendable {
    public let code: UInt8
    public let operation: String

    public init(code: UInt8, operation: String) {
        self.code = code
        self.operation = operation
    }

    public var codeText: String {
        GoProtocol.errorString(code) ?? String(format: "0x%02X", code)
    }
}
