import CoreGraphics
import Foundation
import Testing

import FlowGridCore
@testable import FlowGridGo

/// 传输基础件：TCP 长度前缀、IDENTIFY 编解码、键码表、地址解析与退避参数。
@MainActor
@Suite(.serialized)
final class TransportPrimitivesTests {

    // MARK: - TCP 长度前缀（§7.2）

    @Test func tcpFramingRoundTrip() {
        let frame = Data([0x01, 0x02, 0x03])
        let record = TCPFraming.encode(frame)
        #expect(record.count == frame.count + 2)
        #expect(record[record.startIndex] == 0x00)
        #expect(record[record.index(after: record.startIndex)] == 0x03)

        var buffer = record
        #expect(TCPFraming.takeRecord(from: &buffer) == frame)
        #expect(buffer.isEmpty)
        #expect(TCPFraming.takeRecord(from: &buffer) == nil)
    }

    @Test func tcpFramingPartialAndMultiple() {
        let a = TCPFraming.encode(Data([0xAA]))
        let b = TCPFraming.encode(Data([0xBB, 0xCC]))

        // 头不完整 / 载荷不完整都返回 nil（等待后续字节）
        var headerOnly = Data([0x00])
        #expect(TCPFraming.takeRecord(from: &headerOnly) == nil)
        var payloadCut = Data([0x00, 0x03, 0xAA])
        #expect(TCPFraming.takeRecord(from: &payloadCut) == nil)

        var buffer = a + b
        #expect(TCPFraming.takeRecord(from: &buffer) == Data([0xAA]))
        #expect(TCPFraming.takeRecord(from: &buffer) == Data([0xBB, 0xCC]))
        #expect(buffer.isEmpty)
    }

    // MARK: - IDENTIFY（§4.2：seq_init UInt32 BE、名称 NUL 结尾）

    @Test func identifyRoundTrip() throws {
        let payload = try GoIdentify.encode(
            platform: .macOS, role: 0x03,
            capabilities: [.directLink, .clipboard, .fileTransfer],
            seqInit: 0xBEEF, name: "测试 Mac ✨"
        )
        let parsed = try #require(GoIdentify.parse(payload))
        #expect(parsed.version == 1)
        #expect(parsed.platform == .macOS)
        #expect(parsed.role == 0x03)
        #expect(parsed.capabilities.contains(.clipboard))
        #expect(parsed.seqInit == 0xBEEF)
        #expect(parsed.name == "测试 Mac ✨")
    }

    @Test func identifyResponseRoundTrip() throws {
        let payload = try GoIdentify.encodeResponse(
            status: 0x03, platform: .windows,
            capabilities: [.directLink], name: "Office PC"
        )
        let parsed = try #require(GoIdentify.parseResponse(payload))
        #expect(parsed.status == 0x03)
        #expect(parsed.platform == .windows)
        #expect(parsed.capabilities.contains(.directLink))
        #expect(parsed.name == "Office PC")
    }

    @Test func identifyRejectsMalformed() {
        #expect(GoIdentify.parse(Data()) == nil)
        #expect(GoIdentify.parse(Data([2, 1, 3, 2, 0, 0, 0, 1, 0])) == nil, "版本必须为 1")
        #expect(GoIdentify.parseResponse(Data([0, 1])) == nil)
    }

    // MARK: - 键码表（§6.1/§6.4：US 布局）

    @Test func keyCodeTableRoundTrip() {
        #expect(KeyCodeTable.hidUsage(forVirtualKey: 0x00) == 0x04) // A
        #expect(KeyCodeTable.hidUsage(forVirtualKey: 0x31) == 0x2C) // Space
        #expect(KeyCodeTable.hidUsage(forVirtualKey: 0x24) == 0x28) // Return
        #expect(KeyCodeTable.hidUsage(forVirtualKey: 0x7E) == 0x52) // Up

        #expect(KeyCodeTable.virtualKey(forHID: 0xE3) == 0x37) // ⌘
        #expect(KeyCodeTable.virtualKey(forHID: 0xE0) == 0x3B) // Ctrl

        // 双向一致性抽样
        for vk: CGKeyCode in [0x00, 0x0C, 0x12, 0x1D, 0x31, 0x35, 0x37, 0x3B, 0x7B] {
            guard let hid = KeyCodeTable.hidUsage(forVirtualKey: vk) else {
                Issue.record("VK \(vk) 应有 HID 映射")
                continue
            }
            #expect(KeyCodeTable.virtualKey(forHID: hid) == vk)
        }
        #expect(KeyCodeTable.hidUsage(forVirtualKey: 0xFFFF) == nil)
    }

    @Test func modifierMaskRoundTrip() {
        #expect(KeyCodeTable.modifierMask(from: [.maskCommand]) == FGModifier.leftMeta)
        #expect(KeyCodeTable.modifierMask(from: [.maskShift]) == FGModifier.leftShift)
        #expect(KeyCodeTable.modifierMask(from: [.maskAlternate]) == FGModifier.leftAlt)
        #expect(KeyCodeTable.modifierMask(from: [.maskControl]) == FGModifier.leftCtrl)

        // 通用修饰位归一为左键位（HID 键码仅推断侧别，见 §6.2）
        let mask: UInt8 = FGModifier.ctrl | FGModifier.alt
        #expect(KeyCodeTable.modifierMask(from: KeyCodeTable.flags(fromModifiers: mask))
            == FGModifier.leftCtrl | FGModifier.leftAlt)
    }

    // MARK: - 地址解析与重连退避

    @Test func parseAddress() {
        let explicit = DirectLinkManager.parseAddress("192.168.1.5:9999")
        #expect(explicit.host == "192.168.1.5")
        #expect(explicit.port == 9999)

        let defaulted = DirectLinkManager.parseAddress("office-pc.local")
        #expect(defaulted.host == "office-pc.local")
        #expect(defaulted.port == FGProtocolConstants.defaultPort)
    }

    @Test func loopbackDetection() {
        #expect(DirectLinkManager.isLoopback("127.0.0.1"))
        #expect(DirectLinkManager.isLoopback("127.8.8.8"))
        #expect(DirectLinkManager.isLoopback("localhost"))
        #expect(DirectLinkManager.isLoopback("::1"))
        #expect(!DirectLinkManager.isLoopback("192.168.1.5"))
    }

    @Test func reconnectBackoffBounds() {
        // 200ms ×2 指数、上限 5s、±20% 抖动
        for attempt in 1...8 {
            let delay = Double(DirectLinkManager.backoffMilliseconds(attempt: attempt))
            let base = min(200.0 * pow(2, Double(attempt - 1)), 5_000.0)
            #expect(delay >= base * 0.8 - 1)
            #expect(delay <= base * 1.2 + 1)
        }
    }
}
