// swift-tools-version: 6.0
import PackageDescription

let package = Package(
    name: "YepSVG",
    defaultLocalization: "en",
    platforms: [
        .iOS(.v15)
    ],
    products: [
        .library(name: "YepSVG", targets: ["YepSVG"]),
        .library(name: "YepSVGSwiftUI", targets: ["YepSVGSwiftUI"])
    ],
    targets: [
        .target(
            name: "YepSVGCore",
            path: "Sources/YepSVGCore",
            publicHeadersPath: "include",
            cxxSettings: [
                .headerSearchPath("include"),
                .define("CSVG_IOS")
            ],
            linkerSettings: [
                .linkedFramework("CoreGraphics"),
                .linkedFramework("CoreText")
            ]
        ),
        .target(
            name: "YepSVGCBridge",
            dependencies: ["YepSVGCore"],
            path: "Sources/YepSVGCBridge",
            publicHeadersPath: "include",
            cSettings: [
                .headerSearchPath("include")
            ],
            cxxSettings: [
                .headerSearchPath("include")
            ]
        ),
        .target(
            name: "YepSVG",
            dependencies: ["YepSVGCBridge"],
            path: "Sources/YepSVG",
            linkerSettings: [
                .linkedFramework("UIKit")
            ]
        ),
        .target(
            name: "YepSVGSwiftUI",
            dependencies: ["YepSVG"],
            path: "Sources/YepSVGSwiftUI",
            linkerSettings: [
                .linkedFramework("SwiftUI")
            ]
        ),
        .testTarget(
            name: "YepSVGTests",
            dependencies: ["YepSVG"],
            path: "Tests/YepSVGTests"
        ),
        .testTarget(
            name: "YepSVGParityTests",
            dependencies: ["YepSVG"],
            path: "Tests/YepSVGParityTests"
        )
    ],
    cxxLanguageStandard: .cxx17
)
