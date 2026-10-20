import CFlowGrid
import Foundation

/// 载荷构造与解包：对应桥的 fg_payload_* 一族。
public enum GoPayloads {

    // MARK: - 构造

    public static func keyDown(keyCode: UInt16, modifiers: UInt8) throws -> Data {
        var outLen = 0
        let out = fg_payload_keydown(keyCode, modifiers, &outLen)
        guard let out else { throw GoBridgeError.encodeFailed }
        return fgConsumeBuffer(out, count: outLen) ?? Data()
    }

    public static func keyUp(keyCode: UInt16) throws -> Data {
        var outLen = 0
        let out = fg_payload_keyup(keyCode, &outLen)
        guard let out else { throw GoBridgeError.encodeFailed }
        return fgConsumeBuffer(out, count: outLen) ?? Data()
    }

    public static func mouseMove(deltaX: Int16, deltaY: Int16) throws -> Data {
        var outLen = 0
        let out = fg_payload_mousemove(deltaX, deltaY, &outLen)
        guard let out else { throw GoBridgeError.encodeFailed }
        return fgConsumeBuffer(out, count: outLen) ?? Data()
    }

    public static func mouseButton(buttonID: UInt8, state: UInt8) throws -> Data {
        var outLen = 0
        let out = fg_payload_mousebtn(buttonID, state, &outLen)
        guard let out else { throw GoBridgeError.encodeFailed }
        return fgConsumeBuffer(out, count: outLen) ?? Data()
    }

    public static func scroll(deltaY: Int16, deltaX: Int16) throws -> Data {
        var outLen = 0
        let out = fg_payload_scroll(deltaY, deltaX, &outLen)
        guard let out else { throw GoBridgeError.encodeFailed }
        return fgConsumeBuffer(out, count: outLen) ?? Data()
    }

    public static func latencyPing(timestamp: UInt64) throws -> Data {
        var outLen = 0
        let out = fg_payload_latency_ping(timestamp, &outLen)
        guard let out else { throw GoBridgeError.encodeFailed }
        return fgConsumeBuffer(out, count: outLen) ?? Data()
    }

    public static func latencyPong(timestamp: UInt64) throws -> Data {
        var outLen = 0
        let out = fg_payload_latency_pong(timestamp, &outLen)
        guard let out else { throw GoBridgeError.encodeFailed }
        return fgConsumeBuffer(out, count: outLen) ?? Data()
    }

    public static func disconnect(reason: UInt8) throws -> Data {
        var outLen = 0
        let out = fg_payload_disconnect(reason, &outLen)
        guard let out else { throw GoBridgeError.encodeFailed }
        return fgConsumeBuffer(out, count: outLen) ?? Data()
    }

    public static func error(code: UInt8, message: String) throws -> Data {
        var outLen = 0
        let out = message.withCString { msg in
            fg_payload_error(code, UnsafeMutablePointer(mutating: msg), &outLen)
        }
        guard let out else { throw GoBridgeError.stringTooLong(max: 253) }
        return fgConsumeBuffer(out, count: outLen) ?? Data()
    }

    /// 单帧剪贴板载荷（FragTot=1）。>MaxClipboardSize 的分段发送属会话层职责，桥暂未导出。
    public static func clipboard(mime: String, data: Data) throws -> Data {
        var dataBytes = [UInt8](data)
        var outLen = 0
        let out = mime.withCString { m in
            dataBytes.withUnsafeMutableBufferPointer { buf in
                fg_payload_clipboard(UnsafeMutablePointer(mutating: m), buf.baseAddress, buf.count, &outLen)
            }
        }
        guard let out else { throw GoBridgeError.encodeFailed }
        return fgConsumeBuffer(out, count: outLen) ?? Data()
    }

    // MARK: - 解包

    public struct KeyDownPayload: Equatable, Sendable {
        public let keyCode: UInt16
        public let modifiers: UInt8
    }

    public static func parseKeyDown(_ data: Data) -> KeyDownPayload? {
        var bytes = [UInt8](data)
        var keyCode: UInt16 = 0
        var modifiers: UInt8 = 0
        let rc: Int32 = bytes.withUnsafeMutableBufferPointer { buf in
            fg_payload_unmarshal_keydown(buf.baseAddress, buf.count, &keyCode, &modifiers)
        }
        guard rc == 0 else { return nil }
        return KeyDownPayload(keyCode: keyCode, modifiers: modifiers)
    }

    public static func parseKeyUp(_ data: Data) -> UInt16? {
        var bytes = [UInt8](data)
        var keyCode: UInt16 = 0
        let rc: Int32 = bytes.withUnsafeMutableBufferPointer { buf in
            fg_payload_unmarshal_keyup(buf.baseAddress, buf.count, &keyCode)
        }
        guard rc == 0 else { return nil }
        return keyCode
    }

    public static func parseMouseMove(_ data: Data) -> (deltaX: Int16, deltaY: Int16)? {
        var bytes = [UInt8](data)
        var dx: Int16 = 0
        var dy: Int16 = 0
        let rc: Int32 = bytes.withUnsafeMutableBufferPointer { buf in
            fg_payload_unmarshal_mousemove(buf.baseAddress, buf.count, &dx, &dy)
        }
        guard rc == 0 else { return nil }
        return (dx, dy)
    }

    public static func parseMouseButton(_ data: Data) -> (buttonID: UInt8, state: UInt8)? {
        var bytes = [UInt8](data)
        var btn: UInt8 = 0
        var state: UInt8 = 0
        let rc: Int32 = bytes.withUnsafeMutableBufferPointer { buf in
            fg_payload_unmarshal_mousebtn(buf.baseAddress, buf.count, &btn, &state)
        }
        guard rc == 0 else { return nil }
        return (btn, state)
    }

    public static func parseScroll(_ data: Data) -> (deltaY: Int16, deltaX: Int16)? {
        var bytes = [UInt8](data)
        var dy: Int16 = 0
        var dx: Int16 = 0
        let rc: Int32 = bytes.withUnsafeMutableBufferPointer { buf in
            fg_payload_unmarshal_scroll(buf.baseAddress, buf.count, &dy, &dx)
        }
        guard rc == 0 else { return nil }
        return (dy, dx)
    }

    public static func parseDisconnect(_ data: Data) -> UInt8? {
        var bytes = [UInt8](data)
        var reason: UInt8 = 0
        let rc: Int32 = bytes.withUnsafeMutableBufferPointer { buf in
            fg_payload_unmarshal_disconnect(buf.baseAddress, buf.count, &reason)
        }
        guard rc == 0 else { return nil }
        return reason
    }

    public struct ClipboardPayload: Equatable, Sendable {
        public let mime: String
        public let data: Data
    }

    public static func parseClipboard(_ data: Data) -> ClipboardPayload? {
        var bytes = [UInt8](data)
        var outMimeLen: UInt8 = 0
        var outMime: UnsafeMutablePointer<UInt8>?
        var outDataLen = 0
        var outData: UnsafeMutablePointer<UInt8>?
        let rc: Int32 = bytes.withUnsafeMutableBufferPointer { buf in
            fg_payload_unmarshal_clipboard(buf.baseAddress, buf.count, &outMimeLen, &outMime, &outDataLen, &outData)
        }
        defer {
            if let outMime { fg_free_buffer(outMime) }
            if let outData { fg_free_buffer(outData) }
        }
        guard rc == 0 else { return nil }
        let mimeData = outMime.map { Data(bytes: $0, count: Int(outMimeLen)) } ?? Data()
        let payloadData = outData.map { Data(bytes: $0, count: outDataLen) } ?? Data()
        guard let mime = String(data: mimeData, encoding: .utf8) else { return nil }
        return ClipboardPayload(mime: mime, data: payloadData)
    }
}
