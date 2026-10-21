import Foundation

/// FFI 调用错误。handle 为 0 是 cgo 桥的失败哨兵；缓冲区语义见 GoMemory。
public enum GoBridgeError: Error, Equatable, Sendable {
    case encodeFailed
    case decodeFailed
    case payloadTooLarge(max: Int)
    case handleInvalid
    case keymapLoadFailed(String)
    case stringTooLong(max: Int)
    case operationFailed
}
