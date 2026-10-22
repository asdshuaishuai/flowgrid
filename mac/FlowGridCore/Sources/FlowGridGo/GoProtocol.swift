import CFlowGrid
import FlowGridCore
import Foundation

/// 零散协议函数：修饰键重映射、RSSI 分级、BLE 广播编解码、错误码文案。
public enum GoProtocol {

    /// macOS/iOS 的 Meta(⌘) ⇄ Windows/Linux 的 Ctrl 转换，其余位直通（协议 §6）。
    public static func remapModifiers(_ modifiers: UInt8, from: FGPlatform, to: FGPlatform) -> UInt8 {
        fg_remap_modifiers(modifiers, from.rawValue, to.rawValue)
    }

    /// 发送侧重映射（协议 §6.3/§11）：把一个捕获的键（HID usage + 修饰掩码）从源
    /// 平台重映射到目标平台，键码与掩码一起换——先用户 keymap 规则后平台默认
    /// Ctrl⇄Meta。能力全在协议层（fg_input_remap_key），客户端不再各自实现。
    /// table 传 nil（句柄 0）走平台默认；context 传 nil 即 global。
    public static func remapCapturedKey(_ hidKey: UInt16, modifiers: UInt8, from: FGPlatform, to: FGPlatform) -> (key: UInt16, mods: UInt8) {
        var outKey: UInt16 = 0
        var outMods: UInt8 = 0
        let rc = fg_input_remap_key(hidKey, modifiers, from.rawValue, to.rawValue, nil, nil, &outKey, &outMods)
        guard rc == 0 else { return (hidKey, modifiers) }
        return (outKey, outMods)
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
