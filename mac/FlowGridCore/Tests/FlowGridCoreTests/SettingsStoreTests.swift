import Foundation
import Testing

@testable import FlowGridCore

/// 设置持久化（settings.yaml）往返与容错。
@Suite(.serialized)
final class SettingsStoreTests {

    private func tempStore() -> SettingsStore {
        SettingsStore(url: FileManager.default.temporaryDirectory
            .appendingPathComponent("fg-settings-\(UUID().uuidString).yaml"))
    }

    @Test func roundTrip() throws {
        let store = tempStore()
        defer { try? FileManager.default.removeItem(at: store.url) }

        var settings = AppSettings()
        settings.preferredLink = .directFirst
        settings.mouseSmoothing = true
        settings.edgeSwitchThresholdPx = 16
        settings.clipboardHistoryLimit = .twoHundred
        settings.receiveDirectory = "~/Downloads/Flow Grid"
        settings.latencyAlertThresholdMs = 9
        settings.latencyAlertConsecutive = 5
        settings.autoSwitchOnAlert = false
        settings.localPairingCode = "4242"

        store.save(settings)
        #expect(FileManager.default.fileExists(atPath: store.url.path))

        let loaded = store.load()
        #expect(loaded == settings)
    }

    @Test func missingFileFallsBackToDefaults() {
        let loaded = tempStore().load()
        #expect(loaded == AppSettings())
        #expect(loaded.localPairingCode == "8421")
    }

    @Test func malformedLinesIgnored() throws {
        let store = tempStore()
        defer { try? FileManager.default.removeItem(at: store.url) }
        let junk = """
        # comment
        clipboardSync: false
        unknownKey: whatever
        edgeSwitchThresholdPx: not-a-number
        localPairingCode: "4242"
        """
        try FileManager.default.createDirectory(
            at: store.url.deletingLastPathComponent(), withIntermediateDirectories: true
        )
        try junk.write(to: store.url, atomically: true, encoding: .utf8)

        let loaded = store.load()
        #expect(loaded.clipboardSync == false)
        #expect(loaded.edgeSwitchThresholdPx == 8) // 非法值保留默认
        #expect(loaded.localPairingCode == "4242")
    }
}
