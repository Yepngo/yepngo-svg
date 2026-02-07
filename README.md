# YepSVG

`YepSVG` is an iOS 15+ SwiftPM library that implements a clean-room SVG rendering engine with Chromium-compatible behavior goals.

## Status

This repository includes:
- Swift API (`SVGRenderer`, `SVGRenderOptions`, external loader protocol, errors).
- C ABI bridge for stable Swift <-> C++ integration.
- C++ pipeline module boundaries (`XmlParser`, `SvgDom`, `StyleResolver`, `GeometryEngine`, `LayoutEngine`, `PaintEngine`, `FilterGraph`, `RasterBackendCG`, `ResourceResolver`, `CompatFlags`).
- Functional rendering for common static primitives (`rect`, `circle`, `ellipse`, `line`, `polygon`, `polyline`, limited `path`, `text`).
- Core filter support validation for: `feGaussianBlur`, `feOffset`, `feBlend`, `feColorMatrix`, `feComposite`, `feFlood`, `feMerge`.
- Unit tests and parity harness scaffold with strict pixel diff threshold (0.5%).

## Install

Add to your `Package.swift`:

```swift
.package(path: "/Users/climbatize/Documents/Dev/yepngo-svg")
```

Then depend on:

```swift
.product(name: "YepSVG", package: "YepSVG")
.product(name: "YepSVGSwiftUI", package: "YepSVG")
```

## Usage

```swift
import YepSVG

let renderer = SVGRenderer()
let image = try await renderer.render(svgString: "<svg width=\"24\" height=\"24\"></svg>", options: .default)
```

With external resources:

```swift
struct Loader: SVGExternalResourceLoader {
    func loadResource(_ request: SVGExternalResourceRequest) async throws -> Data {
        // host policy and retrieval
        Data()
    }
}

var options = SVGRenderOptions.default
options.enableExternalResources = true
let renderer = SVGRenderer(loader: Loader())
```

## Fixtures and Parity

- SVG fixtures: `Fixtures/svg`
- Chromium goldens: `Fixtures/golden/chromium`
- Known deviations: `Fixtures/golden/chromium/known-deviations.json`

Generate Chromium goldens:

```bash
npm i playwright
node Scripts/render_chromium_goldens.js
```

## License

BSD-3-Clause. See `LICENSE`.
