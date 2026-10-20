import AppKit
import FlowGridCore
import FlowGridUI
import SwiftUI

/// 纯菜单栏应用：MenuBarExtra（.window 样式）+ 按需唤出的 520px 浮板。
/// 快捷键（⌘1/2/3 快速切换、⌘K 键位、⌘, 设置、⌘Q 退出）经本地事件监视器实现，
/// 仅在 app 活跃时生效；全局热键随 HAL 阶段（辅助功能权限）落地。
@main
struct FlowGridApp: App {
    @NSApplicationDelegateAdaptor(AppDelegate.self) private var appDelegate

    var body: some Scene {
        MenuBarExtra {
            TrayView(model: appDelegate.container.model)
        } label: {
            Image(nsImage: MenuBarIcon.image)
        }
        .menuBarExtraStyle(.window)
    }
}

@MainActor
final class AppDelegate: NSObject, NSApplicationDelegate {
    let container = DependencyContainer()
    private var panels: PanelController!
    private var shortcutMonitor: Any?

    func applicationDidFinishLaunching(_ notification: Notification) {
        setvbuf(stdout, nil, _IONBF, 0) // 日志无缓冲，便于重定向收集
        panels = PanelController(model: container.model)
        container.model.openPanel = { [weak self] id in
            self?.panels.open(id)
        }
        container.model.quit = {
            NSApp.terminate(nil)
        }
        container.start()

        installShortcutMonitor()
        openDebugPanelIfRequested()
        connectIfRequested()
    }

    func applicationWillTerminate(_ notification: Notification) {
        container.stop()
        if let shortcutMonitor {
            NSEvent.removeMonitor(shortcutMonitor)
        }
    }

    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool {
        false
    }

    // MARK: - 快捷键（app 活跃时，见类型注释）

    private func installShortcutMonitor() {
        shortcutMonitor = NSEvent.addLocalMonitorForEvents(matching: .keyDown) { [weak self] event in
            guard let self, event.modifierFlags.contains(.command) else { return event }
            guard let key = event.charactersIgnoringModifiers?.lowercased() else { return event }

            switch key {
            case "1":
                self.container.model.switchTo(deviceID: AppModel.peerDeviceID)
                return nil
            case "2":
                self.container.model.switchTo(deviceID: AppModel.localDeviceID)
                return nil
            case "w":
                self.panels.closeCurrent()
                return nil
            case "k":
                self.panels.open(.keymap)
                return nil
            case ",":
                self.panels.open(.settings)
                return nil
            case "q":
                NSApp.terminate(nil)
                return nil
            default:
                break
            }
            return event
        }
    }

    // MARK: - 开发辅助：FG_OPEN_PANEL=<id> 启动即开对应浮板

    private func openDebugPanelIfRequested() {
        guard let raw = ProcessInfo.processInfo.environment["FG_OPEN_PANEL"],
              let id = FGPanelID(rawValue: raw)
        else { return }
        panels.open(id)
    }

    // MARK: - 开发辅助：FG_CONNECT=<host>[:port]|<code> 启动即连接对端

    private func connectIfRequested() {
        guard let spec = ProcessInfo.processInfo.environment["FG_CONNECT"] else { return }
        let parts = spec.split(separator: "|", maxSplits: 1).map(String.init)
        let address = parts.first ?? ""
        let code = parts.count > 1 ? parts[1] : ""
        DispatchQueue.main.asyncAfter(deadline: .now() + 1) { [weak self] in
            self?.container.model.connectManually(address: address, code: code)
        }
    }
}

/// 菜单栏图标：用设计稿图标集渲染 link 品牌标（模板色）。
@MainActor
enum MenuBarIcon {
    static let image: NSImage = {
        let renderer = ImageRenderer(content: FGIcon("link", size: 18, color: .black))
        renderer.scale = 2
        if let image = renderer.nsImage {
            image.isTemplate = true
            return image
        }
        let fallback = NSImage(systemSymbolName: "link", accessibilityDescription: "FlowGrid") ?? NSImage()
        fallback.isTemplate = true
        return fallback
    }()
}
