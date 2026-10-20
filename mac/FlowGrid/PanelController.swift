import AppKit
import FlowGridUI
import SwiftUI

/// 无边框浮板窗口：可成为 key（失焦即走的前提）；Esc 关闭（cancelOperation）。
final class KeyablePanel: NSPanel {
    var onCancel: (() -> Void)?

    override var canBecomeKey: Bool { true }
    override var canBecomeMain: Bool { false }

    override func cancelOperation(_ sender: Any?) {
        onCancel?()
    }
}

/// 520px 浮板管理：borderless / floating / 失焦即关 / 单块并存；
/// 定位在菜单栏下方居中（设计稿「唤出即来、失焦即走、浮板平级」）。
@MainActor
final class PanelController: NSObject, NSWindowDelegate {
    private let model: AppModel
    private var panels: [FGPanelID: NSPanel] = [:]
    private weak var current: NSPanel?

    init(model: AppModel) {
        self.model = model
    }

    func open(_ id: FGPanelID) {
        model.refreshPermissions()
        // 单块并存：旧板先收
        current?.orderOut(nil)
        current = nil

        let panel: NSPanel
        if let existing = panels[id] {
            panel = existing
        } else {
            panel = makePanel(for: id)
            panels[id] = panel
        }

        // 入场动效（设计稿：220ms cubic-bezier(.16,1,.3,1)，自上方 8px 滑入 + 淡入；
        // 「减弱动态效果」时只保留淡入）
        let finalOrigin = origin(for: panel)
        let reduceMotion = FGMotion.reduceMotion
        panel.setFrameOrigin(NSPoint(x: finalOrigin.x, y: reduceMotion ? finalOrigin.y : finalOrigin.y + 8))
        panel.alphaValue = 0
        panel.makeKeyAndOrderFront(nil)
        NSApp.activate(ignoringOtherApps: true)
        current = panel

        // 初始不落焦点到文本输入框（macOS 浮板不抢输入焦点）：
        // orderFront 后与动画结束后各清一次，仅当当前焦点是文本编辑器时
        clearTextFocus(in: panel)
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.25) { [weak panel] in
            guard let panel else { return }
            self.clearTextFocus(in: panel)
        }

        NSAnimationContext.runAnimationGroup { context in
            context.duration = reduceMotion ? 0.15 : 0.22
            context.timingFunction = CAMediaTimingFunction(controlPoints: 0.16, 1.0, 0.3, 1.0)
            panel.animator().alphaValue = 1
            if !reduceMotion {
                panel.animator().setFrameOrigin(finalOrigin)
            }
        }
    }

    func close(_ id: FGPanelID) {
        panels[id]?.orderOut(nil)
        if current === panels[id] {
            current = nil
        }
    }

    func closeCurrent() {
        current?.orderOut(nil)
        current = nil
    }

    func closeAll() {
        for panel in panels.values {
            panel.orderOut(nil)
        }
        current = nil
    }

    // MARK: - NSWindowDelegate

    func windowDidBecomeKey(_ notification: Notification) {
        guard let panel = notification.object as? NSPanel else { return }
        // 窗口真正成为 key 后，若焦点仍落在文本编辑器则清掉（浮板不抢输入焦点）
        DispatchQueue.main.async { [weak self] in
            self?.clearTextFocus(in: panel)
        }
    }

    func windowDidResignKey(_ notification: Notification) {
        guard let panel = notification.object as? NSPanel, panel === current else { return }
        // 失焦即走（菜单弹出等瞬时失焦由 keyWindow 判断兜底）
        if NSApp.keyWindow == nil || NSApp.keyWindow == panel {
            panel.orderOut(nil)
            current = nil
        }
    }

    // MARK: - 构造

    private func makePanel(for id: FGPanelID) -> NSPanel {
        let hosting = NSHostingController(rootView: AnyView(makeContentView(for: id)))
        hosting.sizingOptions = [.preferredContentSize]

        let panel = KeyablePanel(
            contentRect: NSRect(x: 0, y: 0, width: FGGeometry.panelWidth, height: 480),
            styleMask: [.borderless],
            backing: .buffered,
            defer: false
        )
        panel.isFloatingPanel = true
        panel.level = .floating
        panel.isOpaque = false
        panel.backgroundColor = .clear
        panel.hasShadow = true // 系统窗影替代设计稿的 CSS 阴影
        panel.isMovableByWindowBackground = true
        panel.hidesOnDeactivate = false
        panel.isReleasedWhenClosed = false
        panel.animationBehavior = .utilityWindow
        panel.delegate = self
        panel.contentViewController = hosting
        panel.onCancel = { [weak self] in
            self?.close(id)
        }

        hosting.view.wantsLayer = true
        hosting.view.layer?.cornerRadius = FGRadius.surface
        hosting.view.layer?.masksToBounds = true

        // 按内容拟合高度（上限 = 屏幕高 − 48，超出后由面板内部滚动）
        let maxHeight = (NSScreen.main?.visibleFrame.height ?? 900) - FGGeometry.panelMaxHeightInset
        hosting.view.layoutSubtreeIfNeeded()
        let fitting = hosting.view.fittingSize
        let height = fitting.height > 1 ? min(fitting.height, maxHeight) : 480
        panel.setContentSize(NSSize(width: FGGeometry.panelWidth, height: height))
        return panel
    }

    private func makeContentView(for id: FGPanelID) -> some View {
        let maxHeight = (NSScreen.main?.visibleFrame.height ?? 900) - FGGeometry.panelMaxHeightInset
        return screenView(for: id)
            .frame(maxHeight: maxHeight)
    }

    @ViewBuilder
    private func screenView(for id: FGPanelID) -> some View {
        let onClose: () -> Void = { [weak self] in self?.close(id) }
        switch id {
        case .connections:
            ConnectionsView(model: model, onClose: onClose)
        case .pairing:
            PairingView(model: model, onClose: onClose)
        case .latency:
            LatencyView(model: model, onClose: onClose)
        case .keymap:
            KeyMapView(model: model, onClose: onClose)
        case .clipboard:
            ClipboardView(model: model, onClose: onClose)
        case .files:
            FilesView(model: model, onClose: onClose)
        case .settings:
            SettingsView(model: model, onClose: onClose)
        }
    }

    // MARK: - 定位（菜单栏下方居中；跟随鼠标所在屏幕，多显示器更自然）

    /// 仅当焦点在文本编辑器（TextField 的 field editor）时清除，避免误伤用户点击。
    private func clearTextFocus(in panel: NSPanel) {
        if panel.firstResponder is NSTextView {
            panel.makeFirstResponder(nil)
        }
    }

    private func origin(for panel: NSPanel) -> NSPoint {
        let mouse = NSEvent.mouseLocation
        let screen = NSScreen.screens.first { NSMouseInRect(mouse, $0.frame, false) }
            ?? NSScreen.main
        guard let visible = screen?.visibleFrame else { return panel.frame.origin }
        let size = panel.frame.size
        return NSPoint(
            x: visible.midX - size.width / 2,
            y: visible.maxY - size.height - 4
        )
    }
}
