import CFlowGrid
import FlowGridCore
import Foundation

/// 键位规则表：fg_keymap_load_file / fg_keymap_lookup / fg_keymap_free。
/// 句柄从 1 起；桥内 registry 并发安全，本类可自多线程调用。
public final class GoKeyMap: @unchecked Sendable {
    public let version: String
    public let ruleCount: Int
    private let handle: Int

    public init(contentsOfFile path: String) throws {
        var outVersion: UnsafeMutablePointer<CChar>?
        var outRuleCount = 0
        let h: Int = path.withCString { cPath in
            fg_keymap_load_file(UnsafeMutablePointer(mutating: cPath), &outVersion, &outRuleCount)
        }
        self.handle = try fgRequireHandle(h)
        self.version = fgConsumeKeymapString(outVersion) ?? FGProtocolConstants.keyMapVersion
        self.ruleCount = outRuleCount
    }

    public struct LookupResult: Equatable, Sendable {
        public let keyCode: UInt16
        public let modifiers: UInt8
        public let found: Bool
    }

    /// context 传 nil/空串 = 全局规则；App 上下文用 "app:<bundleID>"，游戏用 "game"。
    public func lookup(from: FGPlatform, to: FGPlatform, keyCode: UInt16, modifiers: UInt8, context: String? = nil) throws -> LookupResult {
        var outKeyCode: UInt16 = 0
        var outModifiers: UInt8 = 0
        var outFound: Int32 = 0
        let ctx = context ?? ""
        let rc: Int32 = ctx.withCString { cCtx in
            fg_keymap_lookup(
                fgKeymapHandlePointer(handle),
                from.rawValue, to.rawValue, keyCode, modifiers,
                UnsafeMutablePointer(mutating: cCtx),
                &outKeyCode, &outModifiers, &outFound
            )
        }
        guard rc == 0 else { throw GoBridgeError.handleInvalid }
        return LookupResult(keyCode: outKeyCode, modifiers: outModifiers, found: outFound != 0)
    }

    deinit {
        if let ptr = fgKeymapHandlePointer(handle) {
            fg_keymap_free(ptr)
        }
    }
}
