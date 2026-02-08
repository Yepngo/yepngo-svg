# AGENTS.md

This file provides guidance to WARP (warp.dev) when working with code in this repository.

## Project Overview

YepSVG is an iOS 15+ Swift Package implementing a clean-room SVG rendering engine with Chromium-compatible behavior goals. It uses a C++ core with a C ABI bridge for Swift interop.

## Build & Test Commands

```bash
# Build
swift build

# Run all tests (requires iOS Simulator)
swift test --destination "platform=iOS Simulator,name=iPhone 16"

# Or using xcodebuild
xcodebuild test \
  -scheme ChromiumSVGKit-Package \
  -destination "platform=iOS Simulator,name=iPhone 16" \
  -skipPackagePluginValidation

# Run a single test
swift test --filter YepSVGTests.testRenderSimpleRectReturnsImage

# Generate Chromium reference goldens (for parity tests)
npm i playwright
node Scripts/render_chromium_goldens.js
```

## Architecture

### Layer Overview

```
┌─────────────────────────────────────────────────────────────┐
│  Swift Public API (YepSVG)                                  │
│    SVGRenderer, SVGRenderOptions, SVGRenderError            │
├─────────────────────────────────────────────────────────────┤
│  C ABI Bridge (YepSVGCBridge)                               │
│    chromium_svg_c_bridge.h - stable C interface             │
├─────────────────────────────────────────────────────────────┤
│  C++ Core Pipeline (YepSVGCore)                             │
│    XmlParser → SvgDom → StyleResolver → GeometryEngine      │
│    → LayoutEngine → PaintEngine → FilterGraph               │
│    → RasterBackendCG                                        │
└─────────────────────────────────────────────────────────────┘
```

### Key Modules

**Swift Layer** (`Sources/YepSVG/`):
- `SVGRenderer.swift` - Main async API entry point; handles external resource preflight
- `SVGCoreBridge.swift` - Marshals data between Swift and C bridge, maps error codes
- `SVGTypes.swift` - Public types: `SVGRenderOptions`, `SVGRenderError`, `SVGExternalResourceLoader`

**C++ Core** (`Sources/YepSVGCore/`):
- `Engine.cpp` - Orchestrates the full render pipeline
- `XmlParser.cpp` - Parses SVG/XML with DOCTYPE entity support
- `StyleResolver.cpp` - CSS cascade, `currentColor`, inheritance
- `GeometryEngine.cpp` - Path commands, transforms, shapes
- `FilterGraph.cpp` - SVG filter primitives (feGaussianBlur, feBlend, etc.)
- `RasterBackendCG.cpp` - CoreGraphics bitmap output
- `CompatFlags.hpp` - Chromium compatibility behavior flags

**Integration Targets**:
- `YepSVGSwiftUI` - `SVGImage` View for SwiftUI
- `YepSVGKingfisher` - `YepSVGProcessor` for Kingfisher image loading

### Rendering Flow

1. Swift receives SVG data/string/URL via `SVGRenderer`
2. External resource URLs are preflighted if `enableExternalResources` is true
3. Data is passed through C bridge to `csvg::Engine::Render()`
4. C++ pipeline: parse XML → build DOM → resolve styles → compute geometry → rasterize
5. RGBA pixel buffer returned via C bridge, converted to `UIImage`

## Test Structure

- **Unit Tests** (`Tests/YepSVGTests/`) - Isolated feature tests with pixel sampling
- **Parity Tests** (`Tests/YepSVGParityTests/`) - Compare rendered output against Chromium goldens (0.5% diff threshold)

Tests use pixel sampling helpers (`pixelAt`, `opaqueBounds`, etc.) defined at bottom of test files.

## Fixtures

- `Fixtures/svg/` - SVG test files
- `Fixtures/golden/chromium/` - Chromium-rendered PNG references
- `Fixtures/golden/chromium/known-deviations.json` - Expected differences
- `Examples/YepSVGSampleApp/Resources/W3CSuite/` - W3C SVG 1.1 test suite

## C++/Swift Interop Notes

- C++ standard: C++17 (`cxxLanguageStandard: .cxx17`)
- All C++ types cross the boundary through C structs defined in `chromium_svg_c_bridge.h`
- Memory for `csvg_render_result_t.rgba` is allocated in C++ and freed via `csvg_render_result_free()`
- Error codes map 1:1 between `csvg_error_code_t` (C) and `SVGRenderError` (Swift)
