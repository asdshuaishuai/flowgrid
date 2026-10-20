import Foundation

/// DirectLink TCP 长度前缀封装（协议 §7.2）：
/// Length(2B BE) + HID Frame(12+N) [+ 8B HMAC]；Length 覆盖帧体与 HMAC 尾。
enum TCPFraming {
    static func encode(_ frame: Data) -> Data {
        precondition(frame.count <= Int(UInt16.max), "record too large")
        var out = Data(capacity: frame.count + 2)
        let length = UInt16(frame.count)
        out.append(UInt8(length >> 8))
        out.append(UInt8(length & 0xFF))
        out.append(frame)
        return out
    }

    /// 从缓冲头部切出完整记录；不足一条返回 nil。
    static func takeRecord(from buffer: inout Data) -> Data? {
        guard buffer.count >= 2 else { return nil }
        let length = Int(buffer[buffer.startIndex]) << 8 | Int(buffer[buffer.index(buffer.startIndex, offsetBy: 1)])
        guard length > 0, buffer.count >= length + 2 else { return nil }
        let start = buffer.index(buffer.startIndex, offsetBy: 2)
        let end = buffer.index(start, offsetBy: length)
        let record = Data(buffer[start..<end])
        buffer.removeSubrange(buffer.startIndex..<end)
        return record
    }
}
