import Foundation
import Testing

import FlowGridCore
import FlowGridGo

/// 会话层 FFI 往返：把 docs/PROTOCOL-INTEGRATION.md §8 的客户端循环
/// （出帧队列 → 自行打序列号/时间戳/HMAC → 对端解码 → 校验序列号 → 分发）
/// 在单进程内完整跑一遍。
@Suite(.serialized)
@MainActor
final class FFISessionTests {

    private let sessionKey = Data("flowgrid-test-session-key".utf8)

    init() {
        GoSession.setSink(false)
    }

    deinit {
        GoSession.setSink(false)
    }

    // MARK: - 出帧循环

    private func drain(
        senderToReceiver: GoSequenceTracker, receiverToSender: GoSequenceTracker,
        receiverChecker: GoSequenceTracker, senderChecker: GoSequenceTracker,
        deliver: (UInt8, Data) throws -> Void
    ) throws -> Int {
        var count = 0
        while let out = GoSession.pollOut() {
            let towardReceiver: Bool = switch out.frameType {
            case FGFrameType.clipboard, FGFrameType.fileOffer, FGFrameType.fileChunk: true
            default: false
            }
            let wire = try GoFrameCodec.encodeWithHMAC(
                frameType: out.frameType,
                sequence: (towardReceiver ? senderToReceiver : receiverToSender).next(),
                timestamp: 1_700_000_000_000,
                payload: out.payload,
                hmacKey: sessionKey
            )
            let decoded = try GoFrameCodec.decodeWithHMAC(wire, hmacKey: sessionKey)
            let checker = towardReceiver ? receiverChecker : senderChecker
            #expect(checker.check(decoded.sequence), "序列号 \(decoded.sequence) 被拒")
            try deliver(decoded.frameType, decoded.payload)
            count += 1
        }
        return count
    }

    private func makeTrackers() throws -> (s1: GoSequenceTracker, s2: GoSequenceTracker, r1: GoSequenceTracker, r2: GoSequenceTracker) {
        (try GoSequenceTracker(seqInit: 0), try GoSequenceTracker(seqInit: 0),
         try GoSequenceTracker(seqInit: 0), try GoSequenceTracker(seqInit: 0))
    }

    // MARK: - 出帧接缝门控

    @Test func sendRejectedWithoutSink() throws {
        let clip = try GoClipboardSession()
        let error = #expect(throws: GoProtocolError.self) {
            try clip.send(mime: "text/plain", data: Data("hello".utf8))
        }
        #expect(error?.code == 0x05, "未声明 sink 应报 notConnected")
        #expect(GoSession.pollOut() == nil)
    }

    @Test func sinkOffClearsQueue() throws {
        GoSession.setSink(true)
        let clip = try GoClipboardSession()
        try clip.send(mime: "text/plain", data: Data("queued".utf8))
        GoSession.setSink(false)
        #expect(GoSession.pollOut() == nil, "撤下 sink 应清空出帧队列")

        GoSession.setSink(true)
        try clip.send(mime: "text/plain", data: Data("again".utf8))
        #expect(GoSession.pollOut() != nil)
    }

    // MARK: - 剪贴板往返

    @Test func clipboardRoundTrip() throws {
        GoSession.setSink(true)
        let outgoing = try GoClipboardSession()
        let incoming = try GoClipboardSession()
        let (s1, s2, r1, r2) = try makeTrackers()

        let payload = Data((0..<60_000).map { UInt8($0 % 251) })
        try outgoing.send(mime: "text/plain", data: payload)

        let frames = try drain(senderToReceiver: s1, receiverToSender: s2, receiverChecker: r1, senderChecker: r2) { type, body in
            #expect(type == FGFrameType.clipboard)
            try incoming.handleFrame(body)
        }
        #expect(frames > 1, "60000 B 应被分段发送")

        let first = try #require(incoming.takePending())
        #expect(first.mime == "text/plain")
        #expect(first.data == payload)
        #expect(incoming.takePending() == nil, "一份剪贴板只应产生一条待取")
    }

    @Test func clipboardLossRepairedBySecondPass() throws {
        GoSession.setSink(true)
        let outgoing = try GoClipboardSession()
        let incoming = try GoClipboardSession()
        let (s1, s2, r1, r2) = try makeTrackers()

        let payload = Data((0..<2_000).map { UInt8($0 % 251) })
        try outgoing.send(mime: "text/plain", data: payload, passes: 2)

        var frameIndex = 0
        _ = try drain(senderToReceiver: s1, receiverToSender: s2, receiverChecker: r1, senderChecker: r2) { _, body in
            frameIndex += 1
            if frameIndex == 2 { return } // 模拟丢包
            try incoming.handleFrame(body)
        }

        let first = try #require(incoming.takePending(), "第二轮应补齐缺段并正好交付一次")
        #expect(first.data == payload)
    }

    // MARK: - 剪贴板只读历史（会话队列）

    @Test func clipboardHistoryView() throws {
        GoSession.setSink(true)
        let outgoing = try GoClipboardSession()
        let incoming = try GoClipboardSession()
        let (s1, s2, r1, r2) = try makeTrackers()

        func roundTrip() throws {
            _ = try drain(senderToReceiver: s1, receiverToSender: s2, receiverChecker: r1, senderChecker: r2) { type, body in
                #expect(type == FGFrameType.clipboard)
                try incoming.handleFrame(body)
            }
        }

        #expect(incoming.droppedCount() == 0, "未溢出前 dropped 为 0")
        #expect(incoming.historyCount() == 0, "空会话历史为 0")

        let first = Data("first-entry".utf8)
        try outgoing.send(mime: "text/plain", data: first)
        try roundTrip()

        #expect(incoming.pendingCount() >= 1, "重组完成应入 pending 队列")
        #expect(incoming.historyCount() == 1, "同一份数据也应入只读历史")

        let e0 = try #require(incoming.history(at: 0))
        #expect(e0.mime == "text/plain")
        #expect(e0.data == first)
        #expect(e0.seq >= 1, "seq 从 1 起单调递增")
        #expect(e0.receivedAtMs > 0, "receivedAtMs 为 Unix 毫秒时间戳")

        // 非破坏性：反复读同一索引不变；越界返回 nil
        #expect(incoming.history(at: 0) == e0, "反复读取不变")
        #expect(incoming.history(at: 1) == nil, "越界返回 nil")
        #expect(incoming.history(at: -1) == nil, "负索引返回 nil")

        // takePending 消费 pending 但不影响历史
        let taken = try #require(incoming.takePending())
        #expect(taken.data == first)
        #expect(incoming.pendingCount() == 0, "取走后 pending 空")
        #expect(incoming.historyCount() == 1, "takePending 不动历史")
        #expect(incoming.history(at: 0)?.data == first, "历史条目仍在")

        // 第二条：历史累积、seq 单调递增、0 = 最旧
        let second = Data("second-entry".utf8)
        try outgoing.send(mime: "text/plain", data: second)
        try roundTrip()
        #expect(incoming.historyCount() == 2)
        let h0 = try #require(incoming.history(at: 0))
        let h1 = try #require(incoming.history(at: 1))
        #expect(h0.data == first, "0 = 最旧")
        #expect(h1.data == second, "1 = 最新")
        #expect(h1.seq > h0.seq, "seq 单调递增")

        // clearHistory 清空只读历史，pending 不受影响（此刻 pending 有 second）
        #expect(incoming.pendingCount() == 1)
        incoming.clearHistory()
        #expect(incoming.historyCount() == 0, "clearHistory 清空只读历史")
        #expect(incoming.history(at: 0) == nil)
        #expect(incoming.pendingCount() == 1, "clearHistory 不动 pending")
        let stillThere = try #require(incoming.takePending())
        #expect(stillThere.data == second)
    }

    // MARK: - 文件传输往返

    @Test func fileTransferRoundTrip() throws {
        GoSession.setSink(true)
        let sender = try GoFileSender()
        let receiver = try GoFileReceiver()
        let (s1, s2, r1, r2) = try makeTrackers()

        let fileData = Data((0..<3_000).map { UInt8($0 % 256) })
        let transferID = try sender.offer(name: "report.pdf", data: fileData)

        _ = try drain(senderToReceiver: s1, receiverToSender: s2, receiverChecker: r1, senderChecker: r2) { type, body in
            let handled = try receiver.handleFrame(frameType: type, payload: body)
            #expect(handled)
        }
        let offer = try #require(receiver.takeOffer())
        #expect(offer.id == transferID)
        #expect(offer.name == "report.pdf")
        #expect(offer.size == UInt64(fileData.count))

        try receiver.accept(id: transferID)

        var finalState: GoFileSender.State = .idle
        var idleRounds = 0
        for _ in 0..<300 {
            let drained = try drain(senderToReceiver: s1, receiverToSender: s2, receiverChecker: r1, senderChecker: r2) { type, body in
                switch type {
                case FGFrameType.fileAck, FGFrameType.fileAbort:
                    try sender.handleFrame(frameType: type, payload: body)
                case FGFrameType.fileOffer, FGFrameType.fileChunk:
                    _ = try receiver.handleFrame(frameType: type, payload: body)
                default:
                    Issue.record("文件传输不应出现帧类型 \(type)")
                }
            }
            let pumped = try sender.pump(maxChunks: 8)
            let snapshot = try sender.poll()
            if snapshot.state == .done || snapshot.state == .failed {
                finalState = snapshot.state
                break
            }
            if drained == 0 && pumped == 0 {
                idleRounds += 1
                if idleRounds > 3 { break }
            } else {
                idleRounds = 0
            }
        }

        #expect(finalState == .done, "传输应完成")
        let completion = try #require(receiver.takeComplete())
        #expect(completion.errorCode == 0, "SHA-256 校验应通过")
        #expect(completion.name == "report.pdf")
        #expect(completion.data == fileData)
    }

    // MARK: - 文件接收进度（非破坏性快照）

    @Test func fileReceiverProgress() throws {
        GoSession.setSink(true)
        let sender = try GoFileSender()
        let receiver = try GoFileReceiver()
        let (s1, s2, r1, r2) = try makeTrackers()

        // 空闲态快照 + 无溢出
        let idle = try #require(receiver.pollProgress())
        #expect(idle.state == 0, "初始为空闲")
        #expect(idle.receivedBytes == 0 && idle.totalBytes == 0)
        #expect(idle.errorCode == 0)
        #expect(receiver.droppedCounts() == (offers: 0, completes: 0), "无溢出")

        let fileData = Data((0..<3_000).map { UInt8($0 % 256) })
        _ = try sender.offer(name: "report.pdf", data: fileData)

        // 先递交 offer 并接受
        _ = try drain(senderToReceiver: s1, receiverToSender: s2, receiverChecker: r1, senderChecker: r2) { type, body in
            _ = try receiver.handleFrame(frameType: type, payload: body)
        }
        try receiver.accept(id: (try #require(receiver.takeOffer())).id)

        // 驱动传输，直到接收侧进度进入终态（2 完成 / 3 失败）
        var finalProgress: GoFileReceiver.Progress?
        for _ in 0..<500 {
            _ = try drain(senderToReceiver: s1, receiverToSender: s2, receiverChecker: r1, senderChecker: r2) { type, body in
                switch type {
                case FGFrameType.fileAck, FGFrameType.fileAbort:
                    try sender.handleFrame(frameType: type, payload: body)
                case FGFrameType.fileOffer, FGFrameType.fileChunk:
                    _ = try receiver.handleFrame(frameType: type, payload: body)
                default:
                    Issue.record("文件传输不应出现帧类型 \(type)")
                }
            }
            _ = try sender.pump(maxChunks: 8)
            if let p = receiver.pollProgress(), p.state == 2 || p.state == 3 {
                finalProgress = p
                break
            }
        }

        let prog = try #require(finalProgress, "传输应进入终态")
        #expect(prog.state == 2, "应成功完成")
        #expect(prog.receivedBytes == fileData.count, "received 应等于总字节")
        #expect(prog.totalBytes == fileData.count)
        #expect(prog.errorCode == 0)

        // 非破坏性：终态轮询不清（takeComplete 才消费）
        #expect(receiver.pollProgress()?.state == 2, "轮询不清终态")

        let completion = try #require(receiver.takeComplete())
        #expect(completion.errorCode == 0, "SHA-256 校验通过")
        #expect(completion.data == fileData)
        #expect(receiver.droppedCounts() == (offers: 0, completes: 0), "无溢出")
    }

    // MARK: - 帧校验错误码语义（未知类型 0x11）

    @Test func validateFrameTypeGuard() throws {
        try GoFrameCodec.validate(frameType: FGFrameType.keyDown, sequence: 0, timestamp: 0, payload: Data([0x04, 0x00, 0x00]))
        let error = #expect(throws: GoProtocolError.self) {
            try GoFrameCodec.validate(frameType: 0x06, sequence: 0, timestamp: 0)
        }
        #expect(error?.code == 0x11)
    }
}
