import AppKit
import FlowGridCore
import FlowGridGo
import FlowGridUI
import SwiftUI

/// 组装 core 组件注入 UI（CLAUDE.md：DependencyContainer in app target）。
@MainActor
final class DependencyContainer {
    let model: AppModel
    let connectionManager: DirectLinkManager

    init() {
        self.connectionManager = DirectLinkManager()

        self.model = AppModel(
            latencyStore: LatencyStore(),
            keyMapRowsLoader: {
                // 用户目录优先（PROTOCOL §13.2 配置路径），否则用包内示例 YAML；
                // 载入失败兜底演示行
                let userPath = NSHomeDirectory() + "/Library/Application Support/FlowGrid/keymap.yaml"
                let path = FileManager.default.fileExists(atPath: userPath)
                    ? userPath
                    : Bundle.main.path(forResource: "keymap", ofType: "yaml")
                if let path, let rows = try? GoKeyMapRows.load(path: path), !rows.isEmpty {
                    return rows
                }
                return DemoData.fallbackKeyMapRows
            }
        )

        let defaultReceiveDir = FileManager.default.homeDirectoryForCurrentUser
            .appendingPathComponent("Downloads/FlowGrid", isDirectory: true)
        connectionManager.updateReceiveDirectory(defaultReceiveDir.path)
        model.settings.receiveDirectory = (defaultReceiveDir.path as NSString).abbreviatingWithTildeInPath
        model.attachConnection(connectionManager)
        connectionManager.onEvent = { [weak model] event in
            model?.handleConnectionEvent(event)
        }
    }

    /// 启动 DirectLink 监听（被控端随时可连）与剪贴板监视。
    func start() {
        connectionManager.startListening(port: FGProtocolConstants.defaultPort)
        model.startPasteboardWatch()
        model.startSettingsAutosave()
        connectionManager.updateLocalPairingCode(model.localPairingCode)
    }

    func stop() {
        model.stopPasteboardWatch()
        model.stopSettingsAutosave()
        connectionManager.disconnect()
    }
}
