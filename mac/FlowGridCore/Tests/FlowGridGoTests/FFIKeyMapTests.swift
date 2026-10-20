import Foundation
import Testing

import FlowGridCore
import FlowGridGo

/// 键位映射：YAML 载入、Lookup 优先级、修饰键重映射（协议 §6 / §11）。
@Suite(.serialized)
@MainActor
final class FFIKeyMapTests {

    private func writeTempKeyMap() throws -> String {
        let yaml = """
        version: "1.0"
        rules:
          - fromOS: windows
            toOS: macos
            fromKey: 0xE0
            toKey: 0xE3
            context: global
          - fromOS: windows
            toOS: macos
            fromKey: 0xE3
            toKey: 0xE2
            context: global
          - fromOS: windows
            toOS: macos
            fromKey: 0xE0
            toKey: 0xE0
            context: app:com.test.vscode
        """
        let url = FileManager.default.temporaryDirectory
            .appendingPathComponent("flowgrid-keymap-\(UUID().uuidString).yaml")
        try yaml.write(to: url, atomically: true, encoding: .utf8)
        return url.path
    }

    private func withTempKeyMap<T>(_ body: (String) throws -> T) throws -> T {
        let path = try writeTempKeyMap()
        defer { try? FileManager.default.removeItem(atPath: path) }
        return try body(path)
    }

    @Test func loadAndMetadata() throws {
        try withTempKeyMap { path in
            let table = try GoKeyMap(contentsOfFile: path)
            #expect(table.version == "1.0")
            #expect(table.ruleCount == 3)
        }
    }

    @Test func globalLookup() throws {
        try withTempKeyMap { path in
            let table = try GoKeyMap(contentsOfFile: path)

            let ctrlToCmd = try table.lookup(from: .windows, to: .macOS, keyCode: 0xE0, modifiers: 0)
            #expect(ctrlToCmd.found)
            #expect(ctrlToCmd.keyCode == 0xE3)
            #expect(ctrlToCmd.modifiers == 0)

            let winToAlt = try table.lookup(from: .windows, to: .macOS, keyCode: 0xE3, modifiers: 0)
            #expect(winToAlt.found)
            #expect(winToAlt.keyCode == 0xE2)

            let unmapped = try table.lookup(from: .windows, to: .macOS, keyCode: 0xE1, modifiers: 0)
            #expect(!unmapped.found)
            #expect(unmapped.keyCode == 0xE1)
        }
    }

    @Test func contextSpecificRuleWins() throws {
        try withTempKeyMap { path in
            let table = try GoKeyMap(contentsOfFile: path)

            let global = try table.lookup(from: .windows, to: .macOS, keyCode: 0xE0, modifiers: 0)
            #expect(global.keyCode == 0xE3)

            let contextual = try table.lookup(
                from: .windows, to: .macOS, keyCode: 0xE0, modifiers: 0,
                context: "app:com.test.vscode"
            )
            #expect(contextual.found)
            #expect(contextual.keyCode == 0xE0)
        }
    }

    @Test func loadMissingFileThrows() {
        let error = #expect(throws: GoBridgeError.self) {
            try GoKeyMap(contentsOfFile: "/nonexistent/flowgrid/keymap.yaml")
        }
        #expect(error == .handleInvalid)
    }

    // MARK: - 修饰键重映射（§6.3：Meta ⇄ Ctrl，Shift/Alt 直通）

    @Test func remapModifiers() {
        #expect(
            GoProtocol.remapModifiers(FGModifier.leftCtrl, from: .windows, to: .macOS)
                == FGModifier.leftMeta
        )
        #expect(
            GoProtocol.remapModifiers(FGModifier.leftMeta, from: .macOS, to: .windows)
                == FGModifier.leftCtrl
        )
        #expect(
            GoProtocol.remapModifiers(FGModifier.rightCtrl, from: .linux, to: .macOS)
                == FGModifier.rightMeta
        )
        #expect(
            GoProtocol.remapModifiers(FGModifier.leftShift | FGModifier.leftAlt, from: .windows, to: .macOS)
                == FGModifier.leftShift | FGModifier.leftAlt
        )
        #expect(
            GoProtocol.remapModifiers(FGModifier.ctrl | FGModifier.alt, from: .windows, to: .linux)
                == FGModifier.ctrl | FGModifier.alt
        )
        #expect(
            GoProtocol.remapModifiers(FGModifier.ctrl | FGModifier.shift, from: .windows, to: .macOS)
                == FGModifier.meta | FGModifier.shift
        )
    }

    // MARK: - 随 app 打包的示例 YAML 可载入且五行探针齐备

    @Test func bundledExampleKeyMapLoads() throws {
        // 包测试无 app bundle：按源码树路径找同一份 YAML
        // #filePath = <repo>/mac/FlowGridCore/Tests/FlowGridGoTests/<file>
        let macRoot = URL(fileURLWithPath: #filePath)
            .deletingLastPathComponent() // FlowGridGoTests
            .deletingLastPathComponent() // Tests
            .deletingLastPathComponent() // FlowGridCore
            .deletingLastPathComponent() // mac
        let yamlURL = macRoot.appendingPathComponent("FlowGrid/Resources/keymap.yaml")
        try #require(FileManager.default.fileExists(atPath: yamlURL.path), "示例 YAML 不在预期路径")

        let table = try GoKeyMap(contentsOfFile: yamlURL.path)
        #expect(table.ruleCount == 5)
        let rows = try GoKeyMapRows.load(path: yamlURL.path)
        #expect(rows.count == 5)
        #expect(rows.allSatisfy { !$0.isGhost }, "示例 YAML 应覆盖全部探针")
        #expect(rows.first?.toKey == "⌘")
    }
}
