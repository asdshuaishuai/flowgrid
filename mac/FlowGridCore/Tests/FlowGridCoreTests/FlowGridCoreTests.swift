import Foundation
import Testing

@testable import FlowGridCore

// MARK: - 延迟分级（协议 §9.2：<2 好 / 2–4 警告 / >4 差）

@Test func latencyTierBoundaries() {
    #expect(LatencyTier.classify(ms: 1.9) == .good)
    #expect(LatencyTier.classify(ms: 2.0) == .warn)
    #expect(LatencyTier.classify(ms: 3.9) == .warn)
    #expect(LatencyTier.classify(ms: 4.0) == .bad)
    #expect(LatencyTier.classify(ms: 11.6) == .bad)
}

@Test func latencyTextFormat() {
    #expect(2.35.latencyText == "2.4ms")
    #expect(1.0.latencyText == "1.0ms")
}

// MARK: - 剪贴板分段（协议 §5.3 公式）

@Test func clipboardSegmentMath() {
    // 空剪贴板占位：仍按单帧处理
    #expect(DemoData.clipboardCurrent.segmentCount == 1)
    #expect(DemoData.clipboardCurrent.isSingleFrame)

    // image/png（9 字符）：预算 239；41984 B（≈41 KB）→ 176 段（公式推算）
    let image = DemoData.clipboardHistory.first { $0.id == "clip-4" }!
    #expect(image.segmentCount == 176)

    // 超限条目（手工构造）不参与分段
    let oversize = ClipboardItem(
        id: "x", kind: .image, preview: "big.mov", mime: "video/quicktime",
        sizeBytes: 1_468_006, ageSeconds: 0, oversize: true
    )
    #expect(oversize.segmentCount == 0)
}

@Test func ageTextFormat() {
    #expect(12.ageText == "12s")
    #expect(60.ageText == "1m")
    #expect(540.ageText == "9m")
    #expect(3_600.ageText == "1h")
}

// MARK: - 延迟存储

@MainActor
@Test func latencyStoreApplyAndChart() {
    let store = LatencyStore()
    store.apply(deviceID: "a", ms: 1.5)
    store.apply(deviceID: "a", ms: 2.5)

    #expect(store.entry("a")?.tier == .warn)
    #expect(store.mean("a") == 2.0)

    // 图表固定 12 柱，前部补 0
    let chart = store.chartSamples("a")
    #expect(chart.count == 12)
    #expect(chart[10] == 1.5)
    #expect(chart[11] == 2.5)
    #expect(chart[0] == 0)

    // 超过 12 个采样只保留最近 12
    for i in 0..<20 { store.apply(deviceID: "a", ms: Double(i)) }
    #expect(store.chartSamples("a") == (8..<20).map(Double.init))
}

// MARK: - 传输文案

@Test func transferDisplayTexts() {
    let item = TransferItem(
        id: "t", name: "f", direction: .sending, peerName: "p",
        sizeBytes: 13_002_342, progress: 0.68, etaSeconds: 4
    )
    #expect(item.sizeText == "12.4 MB")
    #expect(item.percentText == "68%")
    #expect(item.etaText == "剩余 4s")

    let oversize = TransferItem(
        id: "o", name: "backup.img", direction: .sending, peerName: "p",
        sizeBytes: 4_509_715_661, progress: 0, state: .oversize,
        note: "超出 16 MiB 上限，未在链路上传输"
    )
    #expect(oversize.sizeText == "4.2 GB")
    #expect(oversize.note?.contains("16 MiB") == true)
}

// MARK: - 设置默认值对齐协议

@Test func settingsProtocolDefaults() {
    let settings = AppSettings()
    #expect(settings.latencyAlertThresholdMs == 5.0)          // LatencyAlertThresholdMs
    #expect(settings.latencyAlertConsecutive == 3)
    #expect(settings.clipboardHistoryLimit == .fifty)
    #expect(FGProtocolConstants.maxClipboardSize == 63_240)
    #expect(FGProtocolConstants.maxFileSize == 16 << 20)
    #expect(FGProtocolConstants.maxFileChunk == 249)
}

// MARK: - 样例数据与兜底资源

@Test func demoDataShape() {
    #expect(DemoData.clipboardHistory.count == 4)
    #expect(DemoData.pairCodeDigits.count == 4)
    #expect(DemoData.fallbackKeyMapRows.count == 6)
    #expect(DemoData.fallbackKeyMapRows.last?.isGhost == true)
    #expect(DemoData.appOverrides.count == 2)
}
