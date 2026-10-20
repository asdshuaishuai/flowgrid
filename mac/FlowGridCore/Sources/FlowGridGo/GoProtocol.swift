import CFlowGrid
import FlowGridCore
import Foundation

/// 零散协议函数：修饰键重映射、RSSI 分级、BLE 广播编解码、错误码文案。
public enum GoProtocol {

    /// macOS/iOS 的 Meta(⌘) ⇄ Windows/Linux 的 Ctrl 转换，其余位直通（协议 §6）。
    public static func remapModifiers(_ modifiers: UInt8, from: FGPlatform, to: FGPlatform) -> UInt8 {
        fg_remap_modifiers(modifiers, from.rawValue, to.rawValue)
    }

    public static func classifyRSSI(_ rssi: Int) -> FGSignalQuality? {
        let raw = fg_classify_rssi(Int32(rssi))
        return FGSignalQuality(rawValue: Int(raw))
    }

    public struct BLEAdvertising: Equatable, Sendable {
        public let protocolVersion: UInt8
        public let platform: FGPlatform
        public let capabilities: FGCapabilities
        public let wifiDirectChannel: UInt16
    }

    public static func encodeBLEAdvertising(protocolVersion: UInt8, platform: FGPlatform, capabilities: FGCapabilities, wifiDirectChannel: UInt16) throws -> Data {
        var outLen = 0
        let out = fg_ble_advertising_encode(protocolVersion, platform.rawValue, capabilities.rawValue, wifiDirectChannel, &outLen)
        guard let out else { throw GoBridgeError.encodeFailed }
        return fgConsumeBuffer(out, count: outLen) ?? Data()
    }

    public static func decodeBLEAdvertising(_ data: Data) throws -> BLEAdvertising {
        var bytes = [UInt8](data)
        var ver: UInt8 = 0
        var platform: UInt8 = 0
        var caps: UInt8 = 0
        var channel: UInt16 = 0
        let rc: Int32 = bytes.withUnsafeMutableBufferPointer { buf in
            fg_ble_advertising_decode(buf.baseAddress, buf.count, &ver, &platform, &caps, &channel)
        }
        guard rc == 0, let plat = FGPlatform(rawValue: platform) else { throw GoBridgeError.decodeFailed }
        return BLEAdvertising(protocolVersion: ver, platform: plat, capabilities: FGCapabilities(rawValue: caps), wifiDirectChannel: channel)
    }

    public static func errorString(_ code: UInt8) -> String? {
        fgConsumeMessage(fg_error_string(code))
    }
}
