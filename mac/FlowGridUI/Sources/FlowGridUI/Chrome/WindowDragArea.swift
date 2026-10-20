import AppKit
import SwiftUI

/// 标题栏拖拽区。
/// borderless NSPanel 的 `isMovableByWindowBackground` 在 NSHostingView 承载
/// SwiftUI 内容时不生效（鼠标事件被 hosting view 消费），需显式 performDrag。
struct WindowDragArea: NSViewRepresentable {
    func makeNSView(context: Context) -> NSView {
        DragView()
    }

    func updateNSView(_ nsView: NSView, context: Context) {}
}

private final class DragView: NSView {
    /// 非 key 窗口的标题栏点击也应直接进入拖拽（浮板常态）。
    override func acceptsFirstMouse(for event: NSEvent?) -> Bool {
        true
    }

    override func mouseDown(with event: NSEvent) {
        window?.performDrag(with: event)
    }
}
