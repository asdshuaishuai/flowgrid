import Foundation
import Testing

import FlowGridCore
import FlowGridGo

/// 控制类对象的 FFI 往返：延迟 monitor、序列号、心跳、BLE 广播、RSSI 分级、错误码文案。
@Suite(.serialized)
@MainActor
final class FFIControlTests {

    // MARK: - 延迟 monitor（半 RTT；拒绝未来/不匹配/超界样本）

    @Test func latencyMonitorSamplesAndStats() throws {
        let monitor = try GoLatencyMonitor(maxSamples: 16)

        // 合法样本：RTT 3ms → 单程 1.5ms
        monitor.recordPing(sendTimeUs: 1_000_000)
        let sample = try #require(monitor.recordPong(pongTimestampUs: 1_000_000, nowUs: 1_003_000))
        #expect(abs(sample.ms - 1.5) < 0.001)
        #expect(monitor.latest()?.quality == .good)

        // 未来时间戳（pong > now）→ 拒绝
        monitor.recordPing(sendTimeUs: 2_000_000)
        #expect(monitor.recordPong(pongTimestampUs: 2_000_000, nowUs: 1_999_000) == nil)

        // 与最近 ping 不匹配 → 拒绝
        monitor.recordPing(sendTimeUs: 3_000_000)
        #expect(monitor.recordPong(pongTimestampUs: 2_999_000, nowUs: 3_010_000) == nil)

        // 超界 RTT（60s）→ 拒绝
        monitor.recordPing(sendTimeUs: 4_000_000)
        #expect(monitor.recordPong(pongTimestampUs: 4_000_000, nowUs: 64_000_000) == nil)

        // 统计口径：再喂 2.5ms / 3.0ms
        monitor.recordPing(sendTimeUs: 5_000_000)
        _ = monitor.recordPong(pongTimestampUs: 5_000_000, nowUs: 5_005_000)
        monitor.recordPing(sendTimeUs: 6_000_000)
        _ = monitor.recordPong(pongTimestampUs: 6_000_000, nowUs: 6_006_000)

        let stats = try #require(monitor.stats())
        #expect(abs(stats.min - 1.5) < 0.001)
        #expect(abs(stats.max - 3.0) < 0.001)
        #expect(abs(stats.avg - 2.3333) < 0.01)
    }

    @Test func latencyConsecutiveBadResets() throws {
        let monitor = try GoLatencyMonitor(maxSamples: 16)
        // 单程 6ms > 告警阈值 5ms → 连续 bad
        for index in 0..<3 {
            let base = UInt64(10_000_000 + index * 100_000)
            monitor.recordPing(sendTimeUs: base)
            _ = monitor.recordPong(pongTimestampUs: base, nowUs: base + 12_000)
        }
        #expect(monitor.consecutiveBad() == 3)

        // 好样本清零
        monitor.recordPing(sendTimeUs: 20_000_000)
        _ = monitor.recordPong(pongTimestampUs: 20_000_000, nowUs: 20_003_000)
        #expect(monitor.consecutiveBad() == 0)
    }

    // MARK: - 序列号（首帧 = seq_init；重复/过期拒绝；gap 前移；回绕）

    @Test func sequenceTrackerSemantics() throws {
        let tracker = try GoSequenceTracker(seqInit: 5)
        #expect(tracker.check(5))
        #expect(tracker.check(6))
        #expect(!tracker.check(6), "重复帧应拒绝")
        #expect(!tracker.check(4), "过期帧应拒绝")
        // gap：该帧被丢弃，但期望前移到 received+1（§10.3）
        #expect(!tracker.check(9), "gap 帧丢弃并重同步")
        #expect(tracker.check(10), "重同步后按新期望接受")
        #expect(!tracker.check(8), "重同步后的旧帧应拒绝")
    }

    @Test func sequenceTrackersWrap() throws {
        let tracker = try GoSequenceTracker(seqInit: 65_535)
        #expect(tracker.check(65_535))
        #expect(tracker.check(0), "UInt16 回绕到 0")
        #expect(tracker.check(1))
    }

    @Test func sequenceNextAdvances() throws {
        let tracker = try GoSequenceTracker(seqInit: 100)
        let first = tracker.next()
        let second = tracker.next()
        #expect(second == first &+ 1)
    }

    // MARK: - 心跳（3 次 tick 未收到 → 丢失；收到后复位）

    @Test func heartbeatTracking() throws {
        let tracker = try GoHeartbeatTracker()
        #expect(!tracker.tick())
        #expect(!tracker.tick())
        #expect(tracker.tick(), "第 3 次未收到心跳判定丢失")

        tracker.receivedHeartbeat()
        #expect(!tracker.tick(), "复位后重新计数")
    }

    // MARK: - BLE 广播（§3.1.2，7 字节）

    @Test func bleAdvertisingRoundTrip() throws {
        let data = try GoProtocol.encodeBLEAdvertising(
            protocolVersion: 1,
            platform: .macOS,
            capabilities: [.directLink, .clipboard, .fileTransfer],
            wifiDirectChannel: 36
        )
        #expect(data.count == 7)

        let decoded = try GoProtocol.decodeBLEAdvertising(data)
        #expect(decoded.protocolVersion == 1)
        #expect(decoded.platform == .macOS)
        #expect(decoded.capabilities.contains(.directLink))
        #expect(decoded.capabilities.contains(.clipboard))
        #expect(decoded.wifiDirectChannel == 36)
    }

    @Test func bleAdvertisingInvalidRejected() {
        #expect(throws: GoBridgeError.self) {
            try GoProtocol.decodeBLEAdvertising(Data([1, 2]))
        }
    }

    // MARK: - RSSI 分级（§3.1.5）与错误码文案

    @Test func rssiClassification() {
        #expect(GoProtocol.classifyRSSI(-40) == .excellent)
        #expect(GoProtocol.classifyRSSI(-55) == .good)
        #expect(GoProtocol.classifyRSSI(-70) == .fair)
        #expect(GoProtocol.classifyRSSI(-85) == .weak)
    }

    @Test func errorCodeStrings() {
        #expect(GoProtocol.errorString(0x05) == "notConnected")
        #expect(GoProtocol.errorString(0x12) == "invalidPayload")
        #expect(GoProtocol.errorString(0x20) == "sequenceGap")
    }
}
