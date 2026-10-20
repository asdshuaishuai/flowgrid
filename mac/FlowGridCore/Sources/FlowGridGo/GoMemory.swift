import CFlowGrid
import Foundation

/// cgo 桥内存规则：返回缓冲由调用方经 fg_free_buffer 释放、且只释放一次；
/// NULL 缓冲无需释放（桥侧 fg_free_buffer(NULL) 也安全）。
@inline(__always)
func fgConsumeBuffer(_ ptr: UnsafeMutablePointer<UInt8>?, count: Int) -> Data? {
    defer { if let ptr { fg_free_buffer(ptr) } }
    guard let ptr, count > 0 else { return nil }
    return Data(bytes: ptr, count: count)
}

/// fg_keymap_load_file 的 outVersion 为 C.CString，经 fg_keymap_free_string 释放。
@inline(__always)
func fgConsumeKeymapString(_ ptr: UnsafeMutablePointer<CChar>?) -> String? {
    defer { if let ptr { fg_keymap_free_string(ptr) } }
    guard let ptr else { return nil }
    return String(cString: ptr)
}

/// fg_error_string 的 C.CString，fg_free_buffer 即可释放。
@inline(__always)
func fgConsumeMessage(_ ptr: UnsafeMutablePointer<CChar>?) -> String? {
    defer { if let ptr { fg_free_buffer(ptr) } }
    guard let ptr else { return nil }
    return String(cString: ptr)
}

/// 句柄约定：从 1 起，0 永远表示失败/空。
@inline(__always)
func fgRequireHandle(_ handle: Int) throws -> Int {
    guard handle != 0 else { throw GoBridgeError.handleInvalid }
    return handle
}

@inline(__always)
func fgKeymapHandlePointer(_ handle: Int) -> UnsafeMutableRawPointer? {
    UnsafeMutableRawPointer(bitPattern: handle)
}
