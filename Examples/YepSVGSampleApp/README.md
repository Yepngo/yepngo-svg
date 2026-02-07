# YepSVGSampleApp

An iOS SwiftUI sample app for `YepSVG` with two screens:
- `Demo`: renders bundled basic and complex badge SVGs.
- `W3C Harness`: browses SVG 1.1 tests from the W3C suite, renders with `YepSVG`, and compares against W3C PNG references.

## Included Badge SVG

`/Users/climbatize/Documents/Dev/yepngo-assets/Badges/rewards/2025/badge.svg`

## W3C Suite Integration

The app expects this bundled folder:

`Examples/YepSVGSampleApp/YepSVGSampleApp/Resources/W3CSuite`

with:
- `svggen/*.svg`
- `png/full-*.png`
- `htmlEmbedHarness/full-index.html`

Sync from the official archive:

```bash
./Scripts/sync_w3c_svg11_suite.sh
```

Source index:
[W3C SVG 1.1 full index](https://www.w3.org/Graphics/SVG/Test/20061213/htmlEmbedHarness/full-index.html)

## Run

1. Open `Examples/YepSVGSampleApp/YepSVGSampleApp.xcodeproj`
2. Select scheme `YepSVGSampleApp`
3. Run on an iOS Simulator (iOS 15+)

## Regenerate Xcode Project

If you edit `project.yml`:

```bash
cd Examples/YepSVGSampleApp
xcodegen generate
```
