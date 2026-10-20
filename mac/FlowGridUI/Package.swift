// swift-tools-version: 6.0
import PackageDescription

let package = Package(
    name: "FlowGridUI",
    platforms: [.macOS(.v15)],
    products: [
        .library(name: "FlowGridUI", targets: ["FlowGridUI"]),
    ],
    dependencies: [
        .package(path: "../FlowGridCore"),
    ],
    targets: [
        .target(
            name: "FlowGridUI",
            dependencies: [
                .product(name: "FlowGridCore", package: "FlowGridCore"),
            ]
        ),
        .testTarget(name: "FlowGridUITests", dependencies: ["FlowGridUI"]),
    ]
)
