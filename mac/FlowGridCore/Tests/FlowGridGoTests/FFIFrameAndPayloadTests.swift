import Foundation
import Testing

import FlowGridCore
import FlowGridGo

/// 帧完整性（HMAC）与载荷编解码全量往返。
@Suite(.serialized)
@MainActor
final class FFIFrameAndPayloadTests {

    private let key = Data("flowgrid-hmac-test-key".utf8)

    // MARK: - HMAC（协议 §8.3：8 字节截断，尾部不匹配/缺失即拒）

    @Test func hmacRoundTrip() throws {
        let wire = try GoFrameCodec.encodeWithHMAC(
            frameType: FGFrameType.keyDown, sequence: 42, timestamp: 1_700_000_000_000,
            payload: Data([0x04, 0x00, 0x01]), hmacKey: key
        )
        #expect(wire.count == 12 + 3 + 8)

        let decoded = try GoFrameCodec.decodeWithHMAC(wire, hmacKey: key)
        #expect(decoded.frameType == FGFrameType.keyDown)
        #expect(decoded.sequence == 42)
        #expect(decoded.timestamp == 1_700_000_000_000)
        #expect(decoded.payload == Data([0x04, 0x00, 0x01]))
    }

    @Test func hmacTamperRejected() throws {
        var wire = [UInt8](try GoFrameCodec.encodeWithHMAC(
            frameType: FGFrameType.heartbeat, sequence: 0, timestamp: 0,
            payload: Data(), hmacKey: key
        ))
        wire[wire.count - 1] ^= 0xFF
        #expect(throws: GoBridgeError.self) {
            try GoFrameCodec.decodeWithHMAC(Data(wire), hmacKey: key)
        }
    }

    @Test func hmacWrongKeyRejected() throws {
        let wire = try GoFrameCodec.encodeWithHMAC(
            frameType: FGFrameType.heartbeat, sequence: 0, timestamp: 0,
            payload: Data(), hmacKey: key
        )
        #expect(throws: GoBridgeError.self) {
            try GoFrameCodec.decodeWithHMAC(wire, hmacKey: Data("wrong-key".utf8))
        }
    }

    @Test func hmacMissingTailRejected() throws {
        let wire = try GoFrameCodec.encode(frameType: FGFrameType.heartbeat, sequence: 0, timestamp: 0)
        #expect(throws: GoBridgeError.self) {
            try GoFrameCodec.decodeWithHMAC(wire, hmacKey: key)
        }
    }

    @Test func payloadOversizeRejectedAtEncode() {
        let error = #expect(throws: GoBridgeError.self) {
            try GoFrameCodec.encode(
                frameType: FGFrameType.clipboard, sequence: 0, timestamp: 0,
                payload: Data(repeating: 0xAB, count: 256)
            )
        }
        #expect(error == .payloadTooLarge(max: 255))
    }

    // MARK: - 载荷往返

    @Test func keyPayloads() throws {
        let down = try GoPayloads.keyDown(keyCode: 0x04, modifiers: FGModifier.leftCtrl)
        #expect(down.count == 3)
        let parsedDown = try #require(GoPayloads.parseKeyDown(down))
        #expect(parsedDown.keyCode == 0x04)
        #expect(parsedDown.modifiers == FGModifier.leftCtrl)

        let up = try GoPayloads.keyUp(keyCode: 0xE3)
        #expect(up.count == 2)
        #expect(GoPayloads.parseKeyUp(up) == 0xE3)
    }

    @Test func mousePayloads() throws {
        let move = try GoPayloads.mouseMove(deltaX: -5, deltaY: 12)
        #expect(move.count == 4)
        let parsedMove = try #require(GoPayloads.parseMouseMove(move))
        #expect(parsedMove.deltaX == -5)
        #expect(parsedMove.deltaY == 12)

        let button = try GoPayloads.mouseButton(buttonID: 0x01, state: 0x01)
        #expect(button.count == 2)
        let parsedButton = try #require(GoPayloads.parseMouseButton(button))
        #expect(parsedButton.buttonID == 0x01)
        #expect(parsedButton.state == 0x01)

        let scroll = try GoPayloads.scroll(deltaY: -3, deltaX: 7)
        #expect(scroll.count == 4)
        let parsedScroll = try #require(GoPayloads.parseScroll(scroll))
        #expect(parsedScroll.deltaY == -3)
        #expect(parsedScroll.deltaX == 7)
    }

    @Test func controlPayloads() throws {
        let ping = try GoPayloads.latencyPing(timestamp: 0x0102030405060708)
        #expect(ping.count == 8)
        #expect(ping == Data([0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08]))

        let pong = try GoPayloads.latencyPong(timestamp: 0x1122334455667788)
        #expect(pong.count == 8)

        let disconnect = try GoPayloads.disconnect(reason: 0x02)
        #expect(disconnect.count == 1)
        #expect(GoPayloads.parseDisconnect(disconnect) == 0x02)

        let error = try GoPayloads.error(code: 0x12, message: "bad")
        #expect(error == Data([0x12, 0x03]) + Data("bad".utf8))
        let tooLong = #expect(throws: GoBridgeError.self) {
            try GoPayloads.error(code: 0x12, message: String(repeating: "x", count: 254))
        }
        #expect(tooLong == .stringTooLong(max: 253))
    }

    @Test func clipboardSingleFramePayload() throws {
        let data = Data(repeating: 0x41, count: 62)
        let payload = try GoPayloads.clipboard(mime: "text/plain", data: data)
        let parsed = try #require(GoPayloads.parseClipboard(payload))
        #expect(parsed.mime == "text/plain")
        #expect(parsed.data == data)

        // 超过单帧预算（255-(7+10)=238 B）→ 构造失败，必须走会话层分段
        #expect(throws: GoBridgeError.self) {
            try GoPayloads.clipboard(mime: "text/plain", data: Data(repeating: 0x41, count: 300))
        }
    }
}
