// swift-tools-version: 6.0
import Foundation
import PackageDescription

// c-archive 由 scripts/build-go.sh 产出在 mac/Vendor（仓库内相对本包）。
// 测试动作会把本地包编成 dynamic 变体，其自身链接也需要解析 fg_* 符号，
// 故链接参数声明在 FlowGridGo 目标上（而非只放 app 工程）。
let packageDirectory = URL(fileURLWithPath: #filePath).deletingLastPathComponent().path
let vendorDirectory = "\(packageDirectory)/../Vendor"

let package = Package(
    name: "FlowGridCore",
    platforms: [.macOS(.v15)],
    products: [
        .library(name: "FlowGridCore", targets: ["FlowGridCore"]),
        .library(name: "FlowGridGo", targets: ["FlowGridGo"]),
    ],
    targets: [
        // C 暴露层：仅承载 cgo 生成头（flowgrid.h），供 FlowGridGo 引用。
        .target(name: "CFlowGrid"),
        // 纯 Swift：域模型、服务接缝、演示数据。不依赖 FFI，可独立 swift test。
        .target(name: "FlowGridCore"),
        // FFI 封装：对 Go 协议库的安全 RAII 包装。
        .target(
            name: "FlowGridGo",
            dependencies: ["CFlowGrid", "FlowGridCore"],
            linkerSettings: [
                .unsafeFlags(["-L\(vendorDirectory)", "-lflowgrid"], .when(platforms: [.macOS])),
                .linkedFramework("CoreFoundation", .when(platforms: [.macOS])),
            ]
        ),
        .testTarget(name: "FlowGridCoreTests", dependencies: ["FlowGridCore"]),
        // 真实链路集成测试（TCP + 真 Go 会话）：swift-testing，失败报告不走 XCTest 主线程队列
        .testTarget(name: "FlowGridGoTests", dependencies: ["FlowGridGo", "FlowGridCore"]),
    ]
)
