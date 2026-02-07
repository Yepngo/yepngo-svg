import XCTest
import UIKit
import CoreGraphics
@testable import YepSVG

final class YepSVGTests: XCTestCase {
    func testRenderSimpleRectReturnsImage() async throws {
        let renderer = SVGRenderer()
        let svg = """
        <svg width="40" height="30" xmlns="http://www.w3.org/2000/svg">
          <rect x="0" y="0" width="40" height="30" fill="#ff0000"/>
        </svg>
        """

        let image = try await renderer.render(svgString: svg, options: .default)
        XCTAssertEqual(Int(image.size.width), 40)
        XCTAssertEqual(Int(image.size.height), 30)
    }

    func testInvalidDocumentThrows() async {
        let renderer = SVGRenderer()
        do {
            _ = try await renderer.render(svgString: "<html></html>", options: .default)
            XCTFail("Expected invalidDocument error")
        } catch let error as SVGRenderError {
            guard case .invalidDocument = error else {
                XCTFail("Expected invalidDocument, got \(error)")
                return
            }
        } catch {
            XCTFail("Unexpected error type: \(error)")
        }
    }

    func testRenderFromFileURL() async throws {
        let renderer = SVGRenderer()
        let svg = """
        <svg width="12" height="8" xmlns="http://www.w3.org/2000/svg">
          <circle cx="4" cy="4" r="3" fill="blue"/>
        </svg>
        """

        let temp = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString).appendingPathExtension("svg")
        try svg.data(using: .utf8)?.write(to: temp)

        let image = try await renderer.render(svgFileURL: temp, options: .default)
        XCTAssertEqual(Int(image.size.width), 12)
        XCTAssertEqual(Int(image.size.height), 8)
    }

    func testRenderFromFileURLResolvesRelativeImageHref() async throws {
        let renderer = SVGRenderer()
        let root = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString, isDirectory: true)
        try FileManager.default.createDirectory(at: root, withIntermediateDirectories: true)

        let sourceImage = UIGraphicsImageRenderer(size: CGSize(width: 2, height: 2)).image { ctx in
            UIColor.green.setFill()
            ctx.fill(CGRect(x: 0, y: 0, width: 2, height: 2))
        }
        guard let pngData = sourceImage.pngData() else {
            XCTFail("Failed to generate source PNG")
            return
        }

        let imageURL = root.appendingPathComponent("asset.png")
        try pngData.write(to: imageURL)

        let svg = """
        <svg width="2" height="2" xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink">
          <image xlink:href="asset.png" x="0" y="0" width="2" height="2"/>
        </svg>
        """

        let svgURL = root.appendingPathComponent("fixture.svg")
        try svg.data(using: .utf8)?.write(to: svgURL)

        let rendered = try await renderer.render(svgFileURL: svgURL, options: .default)
        guard let cgImage = rendered.cgImage else {
            XCTFail("Missing CGImage")
            return
        }

        let pixel = try pixelAt(cgImage: cgImage, x: 1, y: 1)
        XCTAssertLessThan(pixel.r, 80)
        XCTAssertGreaterThan(pixel.g, 180)
        XCTAssertLessThan(pixel.b, 80)
    }

    func testExternalResourceBlockedWithoutLoader() async {
        let renderer = SVGRenderer()
        let svg = """
        <svg width="20" height="20" xmlns="http://www.w3.org/2000/svg">
          <image href="https://example.com/a.png" x="0" y="0" width="20" height="20"/>
        </svg>
        """

        var options = SVGRenderOptions.default
        options.enableExternalResources = true

        do {
            _ = try await renderer.render(svgString: svg, options: options)
            XCTFail("Expected externalResourceBlocked")
        } catch let error as SVGRenderError {
            guard case .externalResourceBlocked = error else {
                XCTFail("Expected externalResourceBlocked, got \(error)")
                return
            }
        } catch {
            XCTFail("Unexpected error type: \(error)")
        }
    }

    func testExternalResourceLoaderCalled() async throws {
        let loader = MockLoader()
        let renderer = SVGRenderer(loader: loader)
        let svg = """
        <svg width="20" height="20" xmlns="http://www.w3.org/2000/svg">
          <image href="https://example.com/a.png" x="0" y="0" width="20" height="20"/>
        </svg>
        """

        var options = SVGRenderOptions.default
        options.enableExternalResources = true

        _ = try await renderer.render(svgString: svg, options: options)
        let count = await loader.callCount()
        XCTAssertEqual(count, 1)
    }

    func testTopLeftCoordinateSystemIsPreserved() async throws {
        let renderer = SVGRenderer()
        let svg = """
        <svg width="20" height="20" xmlns="http://www.w3.org/2000/svg">
          <rect x="0" y="0" width="20" height="8" fill="#ff0000"/>
          <rect x="0" y="12" width="20" height="8" fill="#0000ff"/>
        </svg>
        """

        let image = try await renderer.render(svgString: svg, options: .default)
        guard let cgImage = image.cgImage else {
            XCTFail("Missing CGImage")
            return
        }

        let topPixel = try pixelAt(cgImage: cgImage, x: 10, y: 2)
        let bottomPixel = try pixelAt(cgImage: cgImage, x: 10, y: 17)

        XCTAssertGreaterThan(topPixel.r, 200)
        XCTAssertLessThan(topPixel.b, 40)

        XCTAssertGreaterThan(bottomPixel.b, 200)
        XCTAssertLessThan(bottomPixel.r, 40)
    }

    func testArcCommandRendersCurvedStroke() async throws {
        let renderer = SVGRenderer()
        let svg = """
        <svg width="24" height="24" xmlns="http://www.w3.org/2000/svg">
          <path d="M4 12 A8 8 0 0 1 20 12" fill="none" stroke="#ff0000" stroke-width="2"/>
        </svg>
        """

        let image = try await renderer.render(svgString: svg, options: .default)
        guard let cgImage = image.cgImage else {
            XCTFail("Missing CGImage")
            return
        }

        let centerPixel = try pixelAt(cgImage: cgImage, x: 12, y: 12)
        XCTAssertLessThan(centerPixel.r, 80, "Arc should not collapse to a center horizontal line")

        let topPixel = try pixelAt(cgImage: cgImage, x: 12, y: 4)
        let bottomPixel = try pixelAt(cgImage: cgImage, x: 12, y: 20)
        XCTAssertTrue(topPixel.r > 120 || bottomPixel.r > 120, "Arc stroke should appear away from the center line")
    }

    func testPathWithFillOnlyStyleRendersWithoutFillRule() async throws {
        let renderer = SVGRenderer()
        let svg = """
        <svg width="48" height="48" xmlns="http://www.w3.org/2000/svg">
          <path d="M8 8 L40 8 L24 40 Z" style="fill:#ff0000;"/>
        </svg>
        """

        let image = try await renderer.render(svgString: svg, options: .default)
        guard let cgImage = image.cgImage else {
            XCTFail("Missing CGImage")
            return
        }

        let inside = try pixelAt(cgImage: cgImage, x: 24, y: 20)
        XCTAssertGreaterThan(inside.r, 200)
        XCTAssertLessThan(inside.g, 50)
        XCTAssertLessThan(inside.b, 50)
    }

    func testBadgeLikePathWithFillOnlyStyleRenders() async throws {
        let renderer = SVGRenderer()
        let svg = """
        <svg width="220" height="220" viewBox="0 0 220 220" xmlns="http://www.w3.org/2000/svg">
          <path d="M53.557,103.497c38.636,-23.339 88.952,-10.921 112.291,27.715c23.339,38.636 10.921,88.952 -27.715,112.291c-38.636,23.339 -88.952,10.921 -112.291,-27.715c-23.339,-38.636 -10.921,-88.952 27.715,-112.291Z" style="fill:#ffd93b;"/>
        </svg>
        """

        let image = try await renderer.render(svgString: svg, options: .default)
        guard let cgImage = image.cgImage else {
            XCTFail("Missing CGImage")
            return
        }

        let center = try pixelAt(cgImage: cgImage, x: 110, y: 130)
        XCTAssertGreaterThan(center.r, 200)
        XCTAssertGreaterThan(center.g, 180)
        XCTAssertLessThan(center.b, 120)
    }

    func testFillOnlyRgbColorRendersWithoutFillRule() async throws {
        let renderer = SVGRenderer()
        let svg = """
        <svg width="32" height="32" xmlns="http://www.w3.org/2000/svg">
          <rect x="4" y="4" width="24" height="24" style="fill:rgb(255,0,0);"/>
        </svg>
        """

        let image = try await renderer.render(svgString: svg, options: .default)
        guard let cgImage = image.cgImage else {
            XCTFail("Missing CGImage")
            return
        }

        let pixel = try pixelAt(cgImage: cgImage, x: 16, y: 16)
        XCTAssertGreaterThan(pixel.r, 200)
        XCTAssertLessThan(pixel.g, 50)
        XCTAssertLessThan(pixel.b, 50)
    }

    func testInheritedFillRuleEvenOddAppliesToChildPath() async throws {
        let renderer = SVGRenderer()
        let svg = """
        <svg width="40" height="40" xmlns="http://www.w3.org/2000/svg" style="fill-rule:evenodd;">
          <path d="M2 2 H38 V38 H2 Z M12 12 H28 V28 H12 Z" style="fill:#ff0000;"/>
        </svg>
        """

        let image = try await renderer.render(svgString: svg, options: .default)
        guard let cgImage = image.cgImage else {
            XCTFail("Missing CGImage")
            return
        }

        let center = try pixelAt(cgImage: cgImage, x: 20, y: 20)
        XCTAssertLessThan(center.a, 20, "Child path should inherit evenodd and leave the hole transparent")
    }

    func testProvidedPathWithoutStyleRendersWhenRootUsesEvenOdd() async throws {
        let renderer = SVGRenderer()
        let svg = """
        <svg width="220" height="220" viewBox="0 0 682 682" xmlns="http://www.w3.org/2000/svg" style="fill-rule:evenodd;">
          <path d="M176.061,298.045c-16.35,12.334 -22.572,38.308 -13.886,57.966c8.686,19.658 29.011,25.603 45.361,13.269c16.35,-12.334 22.572,-38.308 13.886,-57.966c-8.686,-19.658 -29.011,-25.603 -45.361,-13.269Z"/>
        </svg>
        """

        let image = try await renderer.render(svgString: svg, options: .default)
        guard let cgImage = image.cgImage else {
            XCTFail("Missing CGImage")
            return
        }

        let hasOpaque = try regionHasOpaquePixels(cgImage: cgImage, x: 40, y: 80, width: 60, height: 60)
        if !hasOpaque {
            let bounds = try opaqueBounds(cgImage: cgImage)
            XCTFail("Expected provided path to produce visible pixels, opaque bounds=\(String(describing: bounds))")
        }
    }

    func testEllipseHighlightWithFillStyleRendersOverPath() async throws {
        let renderer = SVGRenderer()
        let svg = """
        <svg width="220" height="220" viewBox="0 0 682 682" xmlns="http://www.w3.org/2000/svg" style="fill-rule:evenodd;">
          <g>
            <path d="M176.061,298.045c-16.35,12.334 -22.572,38.308 -13.886,57.966c8.686,19.658 29.011,25.603 45.361,13.269c16.35,-12.334 22.572,-38.308 13.886,-57.966c-8.686,-19.658 -29.011,-25.603 -45.361,-13.269Z"/>
            <ellipse cx="206.971" cy="320.22" rx="8.118" ry="10.607" style="fill:#fff;"/>
          </g>
        </svg>
        """

        let image = try await renderer.render(svgString: svg, options: .default)
        guard let cgImage = image.cgImage else {
            XCTFail("Missing CGImage")
            return
        }

        let center = try pixelAt(cgImage: cgImage, x: 67, y: 103)
        XCTAssertGreaterThan(center.r, 230)
        XCTAssertGreaterThan(center.g, 230)
        XCTAssertGreaterThan(center.b, 230)
        XCTAssertGreaterThan(center.a, 230)
    }

    func testCurrentColorFillRendersUsingColorProperty() async throws {
        let renderer = SVGRenderer()
        let svg = """
        <svg width="20" height="20" xmlns="http://www.w3.org/2000/svg" color="#00ff00">
          <rect x="2" y="2" width="16" height="16" fill="currentColor"/>
        </svg>
        """

        let image = try await renderer.render(svgString: svg, options: .default)
        guard let cgImage = image.cgImage else {
            XCTFail("Missing CGImage")
            return
        }

        let pixel = try pixelAt(cgImage: cgImage, x: 10, y: 10)
        XCTAssertLessThan(pixel.r, 40)
        XCTAssertGreaterThan(pixel.g, 180)
        XCTAssertLessThan(pixel.b, 40)
    }

    func testExtendedNamedColorsRenderForW3CColorPropFixture() async throws {
        let renderer = SVGRenderer()
        let svg = """
        <svg width="120" height="30" xmlns="http://www.w3.org/2000/svg">
          <circle cx="15" cy="15" r="10" fill="crimson"/>
          <circle cx="45" cy="15" r="10" fill="palegreen"/>
          <circle cx="75" cy="15" r="10" fill="royalblue"/>
          <circle cx="105" cy="15" r="10" fill="mediumturquoise"/>
        </svg>
        """

        let image = try await renderer.render(svgString: svg, options: .default)
        guard let cgImage = image.cgImage else {
            XCTFail("Missing CGImage")
            return
        }

        let crimson = try pixelAt(cgImage: cgImage, x: 15, y: 15)
        XCTAssertGreaterThan(crimson.r, 180)
        XCTAssertLessThan(crimson.g, 80)
        XCTAssertLessThan(crimson.b, 120)

        let paleGreen = try pixelAt(cgImage: cgImage, x: 45, y: 15)
        XCTAssertGreaterThan(paleGreen.g, 220)
        XCTAssertGreaterThan(paleGreen.r, 120)
        XCTAssertGreaterThan(paleGreen.b, 120)

        let royalBlue = try pixelAt(cgImage: cgImage, x: 75, y: 15)
        XCTAssertGreaterThan(royalBlue.b, 180)
        XCTAssertGreaterThan(royalBlue.r, 40)
        XCTAssertGreaterThan(royalBlue.g, 60)

        let mediumTurquoise = try pixelAt(cgImage: cgImage, x: 105, y: 15)
        XCTAssertGreaterThan(mediumTurquoise.g, 180)
        XCTAssertGreaterThan(mediumTurquoise.b, 160)
    }

    func testLegacySystemColorKeywordsRenderShapes() async throws {
        let renderer = SVGRenderer()
        let svg = """
        <svg width="120" height="60" xmlns="http://www.w3.org/2000/svg">
          <rect x="0" y="0" width="60" height="60" fill="Background"/>
          <rect x="60" y="0" width="60" height="60" fill="Window"/>
          <rect x="10" y="10" width="40" height="20" fill="ThreeDFace"/>
          <text x="68" y="30" fill="WindowText" font-size="14">A</text>
        </svg>
        """

        let image = try await renderer.render(svgString: svg, options: .default)
        guard let cgImage = image.cgImage else {
            XCTFail("Missing CGImage")
            return
        }

        let background = try pixelAt(cgImage: cgImage, x: 5, y: 5)
        let window = try pixelAt(cgImage: cgImage, x: 90, y: 5)
        let face = try pixelAt(cgImage: cgImage, x: 20, y: 20)

        XCTAssertGreaterThan(background.b, 80)
        XCTAssertLessThan(background.r, 80)
        XCTAssertGreaterThan(window.r, 220)
        XCTAssertGreaterThan(window.g, 220)
        XCTAssertGreaterThan(window.b, 220)
        XCTAssertGreaterThan(face.r, 150)
        XCTAssertGreaterThan(face.g, 150)
        XCTAssertGreaterThan(face.b, 150)
    }

    func testGradientStopCurrentColorUsesGradientColorProperty() async throws {
        let renderer = SVGRenderer()
        let svg = """
        <svg width="120" height="20" xmlns="http://www.w3.org/2000/svg">
          <defs>
            <linearGradient id="grad" color="red" x1="0" y1="0" x2="1" y2="0">
              <stop offset="0%" stop-color="blue"/>
              <stop offset="50%" stop-color="currentColor"/>
              <stop offset="100%" stop-color="yellow"/>
            </linearGradient>
          </defs>
          <rect x="0" y="0" width="120" height="20" fill="url(#grad)"/>
        </svg>
        """

        let image = try await renderer.render(svgString: svg, options: .default)
        guard let cgImage = image.cgImage else {
            XCTFail("Missing CGImage")
            return
        }

        let midpoint = try pixelAt(cgImage: cgImage, x: 60, y: 10)
        XCTAssertGreaterThan(midpoint.r, 200)
        XCTAssertLessThan(midpoint.g, 100)
        XCTAssertLessThan(midpoint.b, 100)
    }

    func testCoordsTrans02FixtureWithLegacyCommentBlockParsesAndRenders() async throws {
        let root = packageRoot()
        let fixture = root.appendingPathComponent("Examples/YepSVGSampleApp/YepSVGSampleApp/Resources/W3CSuite/svggen/coords-trans-02-t.svg")
        XCTAssertTrue(FileManager.default.fileExists(atPath: fixture.path))

        let renderer = SVGRenderer()
        var options = SVGRenderOptions.default
        options.viewportSize = CGSize(width: 480, height: 360)
        options.defaultFontSize = 10

        let image = try await renderer.render(svgFileURL: fixture, options: options)
        guard let cgImage = image.cgImage else {
            XCTFail("Missing CGImage")
            return
        }
        if let data = image.pngData() {
            let debugURL = FileManager.default.temporaryDirectory.appendingPathComponent("yepsvg-debug-coords-units-01-b.png")
            try? data.write(to: debugURL)
            print("DEBUG coords-units render:", debugURL.path)
        }

        let hasVisibleContent = try regionHasOpaquePixels(cgImage: cgImage, x: 10, y: 30, width: 460, height: 320)
        XCTAssertTrue(hasVisibleContent, "Expected coords-trans-02-t fixture to render visible output")
    }

    func testColorProfFixtureAppliesICCProfileToSecondImage() async throws {
        let root = packageRoot()
        let fixture = root.appendingPathComponent("Examples/YepSVGSampleApp/YepSVGSampleApp/Resources/W3CSuite/svggen/color-prof-01-f.svg")
        XCTAssertTrue(FileManager.default.fileExists(atPath: fixture.path))

        let renderer = SVGRenderer()
        var options = SVGRenderOptions.default
        options.viewportSize = CGSize(width: 480, height: 360)
        let image = try await renderer.render(svgFileURL: fixture, options: options)
        guard let cgImage = image.cgImage else {
            XCTFail("Missing CGImage")
            return
        }

        // Top-left sample from first image should stay orange-ish.
        let firstTopLeft = try pixelAt(cgImage: cgImage, x: 60, y: 30)
        XCTAssertGreaterThan(firstTopLeft.r, 150)
        XCTAssertGreaterThan(firstTopLeft.g, 50)
        XCTAssertLessThan(firstTopLeft.b, 80)

        // Same swatch in second image should be ICC-transformed near primary red.
        let secondTopLeft = try pixelAt(cgImage: cgImage, x: 290, y: 120)
        XCTAssertGreaterThan(secondTopLeft.r, 220)
        XCTAssertLessThan(secondTopLeft.g, 60)
        XCTAssertLessThan(secondTopLeft.b, 60)

        // Cyan swatch should remain high G/B and near-zero R after transform.
        let secondCyan = try pixelAt(cgImage: cgImage, x: 290, y: 185)
        XCTAssertLessThan(secondCyan.r, 40)
        XCTAssertGreaterThan(secondCyan.g, 220)
        XCTAssertGreaterThan(secondCyan.b, 220)

        // Ensure transformed second image meaningfully differs from first image.
        let firstCyan = try pixelAt(cgImage: cgImage, x: 60, y: 95)
        let channelDelta = abs(Int(firstCyan.r) - Int(secondCyan.r))
        XCTAssertGreaterThan(channelDelta, 120)
    }

    func testColorProp04FixtureRendersNonTextShapes() async throws {
        let root = packageRoot()
        let fixture = root.appendingPathComponent("Examples/YepSVGSampleApp/YepSVGSampleApp/Resources/W3CSuite/svggen/color-prop-04-t.svg")
        let reference = root.appendingPathComponent("Examples/YepSVGSampleApp/YepSVGSampleApp/Resources/W3CSuite/png/full-color-prop-04-t.png")
        XCTAssertTrue(FileManager.default.fileExists(atPath: fixture.path))
        XCTAssertTrue(FileManager.default.fileExists(atPath: reference.path))

        let renderer = SVGRenderer()
        var options = SVGRenderOptions.default
        options.viewportSize = CGSize(width: 480, height: 360)
        let image = try await renderer.render(svgFileURL: fixture, options: options)
        guard let cgImage = image.cgImage else {
            XCTFail("Missing CGImage")
            return
        }

        guard let referenceImage = UIImage(contentsOfFile: reference.path)?.cgImage else {
            XCTFail("Failed to load reference PNG")
            return
        }

        let samplePoints: [(String, Int, Int)] = [
            ("Background", 12, 12),
            ("AppWorkspace", 30, 30),
            ("Window", 330, 280),
            ("WindowFrame", 88, 180),
            ("ThreeDFace", 100, 175),
            ("Menu", 110, 190),
            ("ThreeDDarkShadow", 207, 200),
            ("ThreeDLightShadow", 120, 169),
            ("ActiveCaption", 250, 85),
            ("ActiveBorder", 92, 90),
            ("ButtonFace", 374, 94),
        ]

        let tolerance = 16
        for (name, x, y) in samplePoints {
            let actual = try pixelAt(cgImage: cgImage, x: x, y: y)
            let expected = try pixelAt(cgImage: referenceImage, x: x, y: y)
            XCTAssertLessThanOrEqual(abs(Int(actual.r) - Int(expected.r)), tolerance, "\(name) red mismatch @(\(x),\(y))")
            XCTAssertLessThanOrEqual(abs(Int(actual.g) - Int(expected.g)), tolerance, "\(name) green mismatch @(\(x),\(y))")
            XCTAssertLessThanOrEqual(abs(Int(actual.b) - Int(expected.b)), tolerance, "\(name) blue mismatch @(\(x),\(y))")
        }
    }

    func testColorProp04FixtureCaptionTextIsLightAndCentered() async throws {
        let root = packageRoot()
        let fixture = root.appendingPathComponent("Examples/YepSVGSampleApp/YepSVGSampleApp/Resources/W3CSuite/svggen/color-prop-04-t.svg")
        XCTAssertTrue(FileManager.default.fileExists(atPath: fixture.path))

        let renderer = SVGRenderer()
        var options = SVGRenderOptions.default
        options.viewportSize = CGSize(width: 480, height: 360)
        let image = try await renderer.render(svgFileURL: fixture, options: options)
        guard let cgImage = image.cgImage else {
            XCTFail("Missing CGImage")
            return
        }

        // Caption text ("Lorem") sits in this strip with fill CaptionText and text-anchor="middle".
        var brightCount = 0
        var xAccumulator = 0
        for y in 68..<112 {
            for x in 170..<310 {
                let pixel = try pixelAt(cgImage: cgImage, x: x, y: y)
                if pixel.a > 120 && pixel.r > 170 && pixel.g > 170 && pixel.b > 170 {
                    brightCount += 1
                    xAccumulator += x
                }
            }
        }

        XCTAssertGreaterThan(brightCount, 80, "Expected bright caption glyph pixels")
        let centerX = Double(xAccumulator) / Double(brightCount)
        XCTAssertLessThan(abs(centerX - 240.0), 25.0, "Caption text should be centered around x=240")
    }

    func testCoordsUnits01PatternDefinitionsDoNotRenderAtTopLeft() async throws {
        let root = packageRoot()
        let fixture = root.appendingPathComponent("Examples/YepSVGSampleApp/YepSVGSampleApp/Resources/W3CSuite/svggen/coords-units-01-b.svg")
        XCTAssertTrue(FileManager.default.fileExists(atPath: fixture.path))

        let renderer = SVGRenderer()
        var options = SVGRenderOptions.default
        options.viewportSize = CGSize(width: 480, height: 360)
        let image = try await renderer.render(svgFileURL: fixture, options: options)
        guard let cgImage = image.cgImage else {
            XCTFail("Missing CGImage")
            return
        }

        // Pattern definition content must not leak into document origin space.
        let leakedPatternPixel = try pixelAt(cgImage: cgImage, x: 25, y: 15)
        XCTAssertFalse(leakedPatternPixel.r > 180 && leakedPatternPixel.g < 100 && leakedPatternPixel.b < 100,
                       "Unexpected red pattern leak at top-left origin")
        XCTAssertFalse(leakedPatternPixel.b > 180 && leakedPatternPixel.r < 120 && leakedPatternPixel.g < 120,
                       "Unexpected blue pattern leak at top-left origin")
    }

    func testCoordsUnits01PatternFillsRenderInPercentageFractionAndUserSpaceRects() async throws {
        let root = packageRoot()
        let fixture = root.appendingPathComponent("Examples/YepSVGSampleApp/YepSVGSampleApp/Resources/W3CSuite/svggen/coords-units-01-b.svg")
        XCTAssertTrue(FileManager.default.fileExists(atPath: fixture.path))

        let renderer = SVGRenderer()
        var options = SVGRenderOptions.default
        options.viewportSize = CGSize(width: 480, height: 360)
        let image = try await renderer.render(svgFileURL: fixture, options: options)
        guard let cgImage = image.cgImage else {
            XCTFail("Missing CGImage")
            return
        }

        let patternRegions: [String: (x: Int, y: Int, width: Int, height: Int)] = [
            "percentage": (30, 250, 50, 30),
            "fraction": (180, 250, 50, 30),
            "user-space": (330, 250, 50, 30),
        ]

        for (name, region) in patternRegions {
            let redCount = try countPixels(cgImage: cgImage, x: region.x, y: region.y, width: region.width, height: region.height) { px in
                px.a > 20 && px.r > 170 && px.g < 120 && px.b < 120
            }
            let blueCount = try countPixels(cgImage: cgImage, x: region.x, y: region.y, width: region.width, height: region.height) { px in
                px.a > 20 && px.b > 170 && px.r < 120 && px.g < 120
            }

            XCTAssertGreaterThan(redCount, 20, "Expected red pattern content in \(name) region")
            XCTAssertGreaterThan(blueCount, 20, "Expected blue pattern content in \(name) region")
        }
    }

    func testCoordsUnits01PatternOrientationMatchesReferenceSamples() async throws {
        let root = packageRoot()
        let fixture = root.appendingPathComponent("Examples/YepSVGSampleApp/YepSVGSampleApp/Resources/W3CSuite/svggen/coords-units-01-b.svg")
        XCTAssertTrue(FileManager.default.fileExists(atPath: fixture.path))

        let renderer = SVGRenderer()
        var options = SVGRenderOptions.default
        options.viewportSize = CGSize(width: 480, height: 360)
        let image = try await renderer.render(svgFileURL: fixture, options: options)
        guard let cgImage = image.cgImage else {
            XCTFail("Missing CGImage")
            return
        }

        let percentageRed = try pixelAt(cgImage: cgImage, x: 35, y: 255)
        let percentageBlue = try pixelAt(cgImage: cgImage, x: 40, y: 255)
        let fractionRed = try pixelAt(cgImage: cgImage, x: 185, y: 255)
        let fractionBlue = try pixelAt(cgImage: cgImage, x: 190, y: 255)
        let userRed = try pixelAt(cgImage: cgImage, x: 340, y: 265)
        let userBlue = try pixelAt(cgImage: cgImage, x: 340, y: 255)

        let percentageBlueHit = try firstPixelMatching(cgImage: cgImage, x: 30, y: 250, width: 50, height: 30) { px in
            px.a > 20 && px.b > 150 && px.r < 120 && px.g < 120
        }
        let fractionBlueHit = try firstPixelMatching(cgImage: cgImage, x: 180, y: 250, width: 50, height: 30) { px in
            px.a > 20 && px.b > 150 && px.r < 120 && px.g < 120
        }

        XCTAssertTrue(percentageRed.r > 160 && percentageRed.b < 140, "Percentage red sample mismatch: \(percentageRed)")
        XCTAssertTrue(percentageBlue.b > 160 && percentageBlue.r < 140, "Percentage blue sample mismatch: \(percentageBlue), first blue hit: \(String(describing: percentageBlueHit))")
        XCTAssertTrue(fractionRed.r > 160 && fractionRed.b < 140, "Fraction red sample mismatch: \(fractionRed)")
        XCTAssertTrue(fractionBlue.b > 160 && fractionBlue.r < 140, "Fraction blue sample mismatch: \(fractionBlue), first blue hit: \(String(describing: fractionBlueHit))")
        XCTAssertTrue(userRed.r > 160 && userRed.b < 140, "User-space red sample mismatch: \(userRed)")
        XCTAssertTrue(userBlue.b > 160 && userBlue.r < 140, "User-space blue sample mismatch: \(userBlue)")
    }

    func testCoordsUnits02FixturePercentageAndCssUnitConversionsMatchReference() async throws {
        let root = packageRoot()
        let fixture = root.appendingPathComponent("Examples/YepSVGSampleApp/YepSVGSampleApp/Resources/W3CSuite/svggen/coords-units-02-b.svg")
        XCTAssertTrue(FileManager.default.fileExists(atPath: fixture.path))

        let renderer = SVGRenderer()
        var options = SVGRenderOptions.default
        options.viewportSize = CGSize(width: 480, height: 360)
        let image = try await renderer.render(svgFileURL: fixture, options: options)
        guard let cgImage = image.cgImage else {
            XCTFail("Missing CGImage")
            return
        }

        // Percentage coordinate marker should overlap the black marker center.
        let percentMarker = try pixelAt(cgImage: cgImage, x: 35, y: 80)
        XCTAssertGreaterThan(percentMarker.r, 180)
        XCTAssertLessThan(percentMarker.g, 80)
        XCTAssertLessThan(percentMarker.b, 80)

        // Percentage width/height rectangle should match the red control marker block.
        let percentRect = try pixelAt(cgImage: cgImage, x: 32, y: 197)
        XCTAssertGreaterThan(percentRect.r, 180)
        XCTAssertLessThan(percentRect.g, 80)
        XCTAssertLessThan(percentRect.b, 80)

        // Percentage radius should produce a green circle with comparable radius.
        let greenMid = try pixelAt(cgImage: cgImage, x: 122, y: 260)
        XCTAssertGreaterThan(greenMid.g, 90)
        XCTAssertLessThan(greenMid.r, 90)
        XCTAssertLessThan(greenMid.b, 90)
        let greenOutside = try pixelAt(cgImage: cgImage, x: 126, y: 260)
        XCTAssertLessThan(greenOutside.a, 30)
    }

    func testCoordsViewattr01FixtureParsesDoctypeEntitiesAndRenders() async throws {
        let root = packageRoot()
        let fixture = root.appendingPathComponent("Examples/YepSVGSampleApp/YepSVGSampleApp/Resources/W3CSuite/svggen/coords-viewattr-01-b.svg")
        XCTAssertTrue(FileManager.default.fileExists(atPath: fixture.path))

        let renderer = SVGRenderer()
        var options = SVGRenderOptions.default
        options.viewportSize = CGSize(width: 480, height: 360)
        let image = try await renderer.render(svgFileURL: fixture, options: options)
        guard let cgImage = image.cgImage else {
            XCTFail("Missing CGImage")
            return
        }

        // Left smile from expanded &Smile; entity.
        let leftSmile = try pixelAt(cgImage: cgImage, x: 35, y: 90)
        XCTAssertGreaterThan(leftSmile.r, 200)
        XCTAssertGreaterThan(leftSmile.g, 200)
        XCTAssertLessThan(leftSmile.b, 80)

        // Smile inside nested <svg> in meet-group.
        let nestedSmile = try pixelAt(cgImage: cgImage, x: 131, y: 91)
        XCTAssertGreaterThan(nestedSmile.r, 200)
        XCTAssertGreaterThan(nestedSmile.g, 200)
        XCTAssertLessThan(nestedSmile.b, 80)
    }

    func testCoordsViewattr02FixtureImagePreserveAspectRatioMatchesReference() async throws {
        let root = packageRoot()
        let fixture = root.appendingPathComponent("Examples/YepSVGSampleApp/YepSVGSampleApp/Resources/W3CSuite/svggen/coords-viewattr-02-b.svg")
        let reference = root.appendingPathComponent("Examples/YepSVGSampleApp/YepSVGSampleApp/Resources/W3CSuite/png/full-coords-viewattr-02-b.png")
        XCTAssertTrue(FileManager.default.fileExists(atPath: fixture.path))
        XCTAssertTrue(FileManager.default.fileExists(atPath: reference.path))

        let renderer = SVGRenderer()
        var options = SVGRenderOptions.default
        options.viewportSize = CGSize(width: 480, height: 360)
        let image = try await renderer.render(svgFileURL: fixture, options: options)
        guard let cgImage = image.cgImage else {
            XCTFail("Missing CGImage")
            return
        }
        guard let referenceImage = UIImage(contentsOfFile: reference.path)?.cgImage else {
            XCTFail("Failed to load reference PNG")
            return
        }

        // Centers of each preserveAspectRatio sample viewport.
        let samplePoints: [(String, Int, Int)] = [
            ("source-none", 40, 90),
            ("meet-xMin", 145, 95),
            ("meet-xMid", 215, 95),
            ("meet-xMax", 145, 145),
            ("meet-YMin", 315, 110),
            ("meet-YMid", 365, 110),
            ("meet-YMax", 415, 110),
            ("slice-xMin", 135, 245),
            ("slice-xMid", 185, 245),
            ("slice-xMax", 235, 245),
            ("slice-YMin", 325, 230),
            ("slice-YMid", 395, 230),
            ("slice-YMax", 325, 280),
        ]
        let tolerance = 20
        for (name, x, y) in samplePoints {
            let actual = try pixelAt(cgImage: cgImage, x: x, y: y)
            let expected = try pixelAt(cgImage: referenceImage, x: x, y: y)
            XCTAssertLessThanOrEqual(abs(Int(actual.r) - Int(expected.r)), tolerance, "\(name) red mismatch @(\(x),\(y))")
            XCTAssertLessThanOrEqual(abs(Int(actual.g) - Int(expected.g)), tolerance, "\(name) green mismatch @(\(x),\(y))")
            XCTAssertLessThanOrEqual(abs(Int(actual.b) - Int(expected.b)), tolerance, "\(name) blue mismatch @(\(x),\(y))")
        }
    }

    func testExtendNamespace01FixtureRendersPieChartFromBusinessData() async throws {
        let root = packageRoot()
        let fixture = root.appendingPathComponent("Examples/YepSVGSampleApp/YepSVGSampleApp/Resources/W3CSuite/svggen/extend-namespace-01-f.svg")
        let reference = root.appendingPathComponent("Examples/YepSVGSampleApp/YepSVGSampleApp/Resources/W3CSuite/png/full-extend-namespace-01-f.png")
        XCTAssertTrue(FileManager.default.fileExists(atPath: fixture.path))
        XCTAssertTrue(FileManager.default.fileExists(atPath: reference.path))

        let renderer = SVGRenderer()
        var options = SVGRenderOptions.default
        options.viewportSize = CGSize(width: 480, height: 360)
        let image = try await renderer.render(svgFileURL: fixture, options: options)
        guard let cgImage = image.cgImage else {
            XCTFail("Missing CGImage")
            return
        }
        guard let referenceImage = UIImage(contentsOfFile: reference.path)?.cgImage else {
            XCTFail("Failed to load reference PNG")
            return
        }

        let samplePoints: [(String, Int, Int)] = [
            ("first-slice-pink", 320, 150),
            ("first-slice-offset", 350, 115),
            ("west-slice-gray", 180, 185),
            ("south-slice-gray", 240, 250),
            ("outside-pie-transparent", 120, 170),
        ]
        let tolerance = 22
        for (name, x, y) in samplePoints {
            let actual = try pixelAt(cgImage: cgImage, x: x, y: y)
            let expected = try pixelAt(cgImage: referenceImage, x: x, y: y)
            XCTAssertLessThanOrEqual(abs(Int(actual.r) - Int(expected.r)), tolerance, "\(name) red mismatch @(\(x),\(y))")
            XCTAssertLessThanOrEqual(abs(Int(actual.g) - Int(expected.g)), tolerance, "\(name) green mismatch @(\(x),\(y))")
            XCTAssertLessThanOrEqual(abs(Int(actual.b) - Int(expected.b)), tolerance, "\(name) blue mismatch @(\(x),\(y))")
            XCTAssertLessThanOrEqual(abs(Int(actual.a) - Int(expected.a)), tolerance, "\(name) alpha mismatch @(\(x),\(y))")
        }
    }

    func testPatternFillKeepsTileOrientation() async throws {
        let renderer = SVGRenderer()
        let svg = """
        <svg width="40" height="20" xmlns="http://www.w3.org/2000/svg">
          <defs>
            <pattern id="quad" patternUnits="userSpaceOnUse" patternContentUnits="userSpaceOnUse" x="0" y="0" width="20" height="20">
              <rect x="0" y="0" width="10" height="10" fill="#ff0000"/>
              <rect x="10" y="0" width="10" height="10" fill="#00ff00"/>
              <rect x="0" y="10" width="10" height="10" fill="#0000ff"/>
              <rect x="10" y="10" width="10" height="10" fill="#ffff00"/>
            </pattern>
          </defs>
          <rect x="0" y="0" width="40" height="20" fill="url(#quad)"/>
        </svg>
        """

        let image = try await renderer.render(svgString: svg, options: .default)
        guard let cgImage = image.cgImage else {
            XCTFail("Missing CGImage")
            return
        }

        let topLeft = try pixelAt(cgImage: cgImage, x: 5, y: 5)
        let topRight = try pixelAt(cgImage: cgImage, x: 15, y: 5)
        let bottomLeft = try pixelAt(cgImage: cgImage, x: 5, y: 15)
        let bottomRight = try pixelAt(cgImage: cgImage, x: 15, y: 15)

        XCTAssertGreaterThan(topLeft.r, 200)
        XCTAssertLessThan(topLeft.g, 80)
        XCTAssertLessThan(topLeft.b, 80)

        XCTAssertLessThan(topRight.r, 80)
        XCTAssertGreaterThan(topRight.g, 200)
        XCTAssertLessThan(topRight.b, 80)

        XCTAssertLessThan(bottomLeft.r, 80)
        XCTAssertLessThan(bottomLeft.g, 80)
        XCTAssertGreaterThan(bottomLeft.b, 200)

        XCTAssertGreaterThan(bottomRight.r, 200)
        XCTAssertGreaterThan(bottomRight.g, 200)
        XCTAssertLessThan(bottomRight.b, 80)
    }

    func testPserversPattern01FixturePatternOrientationSamples() async throws {
        let root = packageRoot()
        let fixture = root.appendingPathComponent("Examples/YepSVGSampleApp/YepSVGSampleApp/Resources/W3CSuite/svggen/pservers-pattern-01-b.svg")
        XCTAssertTrue(FileManager.default.fileExists(atPath: fixture.path))

        let renderer = SVGRenderer()
        var options = SVGRenderOptions.default
        options.viewportSize = CGSize(width: 480, height: 360)
        let image = try await renderer.render(svgFileURL: fixture, options: options)
        guard let cgImage = image.cgImage else {
            XCTFail("Missing CGImage")
            return
        }

        let topRed = try pixelAt(cgImage: cgImage, x: 37, y: 17)
        XCTAssertGreaterThan(topRed.r, 180)
        XCTAssertLessThan(topRed.g, 120)
        XCTAssertLessThan(topRed.b, 120)

        let topGreen = try pixelAt(cgImage: cgImage, x: 43, y: 23)
        XCTAssertLessThan(topGreen.r, 120)
        XCTAssertGreaterThan(topGreen.g, 90)
        XCTAssertLessThan(topGreen.b, 120)
    }

    func testInheritedCurrentColorKeepsParentComputedFillForChildren() async throws {
        let renderer = SVGRenderer()
        let svg = """
        <svg width="40" height="20" xmlns="http://www.w3.org/2000/svg">
          <g fill="currentColor" color="#ff0000">
            <rect x="0" y="0" width="20" height="20"/>
            <g color="#0000ff">
              <rect x="20" y="0" width="20" height="20"/>
            </g>
          </g>
        </svg>
        """

        let image = try await renderer.render(svgString: svg, options: .default)
        guard let cgImage = image.cgImage else {
            XCTFail("Missing CGImage")
            return
        }

        let left = try pixelAt(cgImage: cgImage, x: 10, y: 10)
        let right = try pixelAt(cgImage: cgImage, x: 30, y: 10)

        XCTAssertGreaterThan(left.r, 180)
        XCTAssertLessThan(left.b, 40)
        XCTAssertGreaterThan(right.r, 180)
        XCTAssertLessThan(right.b, 40)
    }

    func testLocalFillCurrentColorUsesElementColor() async throws {
        let renderer = SVGRenderer()
        let svg = """
        <svg width="40" height="20" xmlns="http://www.w3.org/2000/svg">
          <g fill="currentColor" color="#ff0000">
            <rect x="0" y="0" width="20" height="20"/>
            <g color="#0000ff">
              <rect x="20" y="0" width="20" height="20" fill="currentColor"/>
            </g>
          </g>
        </svg>
        """

        let image = try await renderer.render(svgString: svg, options: .default)
        guard let cgImage = image.cgImage else {
            XCTFail("Missing CGImage")
            return
        }

        let left = try pixelAt(cgImage: cgImage, x: 10, y: 10)
        let right = try pixelAt(cgImage: cgImage, x: 30, y: 10)

        XCTAssertGreaterThan(left.r, 180)
        XCTAssertLessThan(left.b, 40)
        XCTAssertGreaterThan(right.b, 180)
        XCTAssertLessThan(right.r, 40)
    }

    func testColorProp05FixtureUsesParentCurrentColorComputedFill() async throws {
        let root = packageRoot()
        let fixture = root.appendingPathComponent("Examples/YepSVGSampleApp/YepSVGSampleApp/Resources/W3CSuite/svggen/color-prop-05-t.svg")
        XCTAssertTrue(FileManager.default.fileExists(atPath: fixture.path))

        let renderer = SVGRenderer()
        var options = SVGRenderOptions.default
        options.viewportSize = CGSize(width: 480, height: 360)
        let image = try await renderer.render(svgFileURL: fixture, options: options)
        guard let cgImage = image.cgImage else {
            XCTFail("Missing CGImage")
            return
        }

        let center = try pixelAt(cgImage: cgImage, x: 200, y: 130)
        XCTAssertGreaterThan(center.r, 220)
        XCTAssertLessThan(center.g, 40)
        XCTAssertLessThan(center.b, 40)
    }

    func testTextAnchorMiddleCentersTextAndFontWeightBoldAddsInk() async throws {
        let renderer = SVGRenderer()

        let centered = """
        <svg width="120" height="40" xmlns="http://www.w3.org/2000/svg">
          <text x="60" y="28" text-anchor="middle" font-size="24" fill="#000">MMMM</text>
        </svg>
        """
        let centeredImage = try await renderer.render(svgString: centered, options: .default)
        guard let centeredCG = centeredImage.cgImage else {
            XCTFail("Missing centered CGImage")
            return
        }
        guard let centeredBounds = try opaqueBounds(cgImage: centeredCG) else {
            XCTFail("Centered text produced no opaque pixels")
            return
        }
        let boundsCenterX = Double(centeredBounds.minX + centeredBounds.maxX) / 2.0
        XCTAssertLessThan(abs(boundsCenterX - 60.0), 3.0)

        let normal = """
        <svg width="180" height="50" xmlns="http://www.w3.org/2000/svg">
          <text x="4" y="34" font-size="32" fill="#000">TEXT</text>
        </svg>
        """
        let bold = """
        <svg width="180" height="50" xmlns="http://www.w3.org/2000/svg">
          <text x="4" y="34" font-size="32" font-weight="bold" fill="#000">TEXT</text>
        </svg>
        """

        let normalImage = try await renderer.render(svgString: normal, options: .default)
        let boldImage = try await renderer.render(svgString: bold, options: .default)
        guard let normalCG = normalImage.cgImage, let boldCG = boldImage.cgImage else {
            XCTFail("Missing CGImage for normal/bold comparison")
            return
        }

        let normalInk = try opaquePixelCount(cgImage: normalCG)
        let boldInk = try opaquePixelCount(cgImage: boldCG)
        XCTAssertGreaterThan(boldInk, normalInk + 80, "Bold text should draw more ink than normal text")
    }

    func testStrokeLineCapRoundExtendsBeyondLineEndpoints() async throws {
        let renderer = SVGRenderer()
        let svg = """
        <svg width="40" height="28" xmlns="http://www.w3.org/2000/svg">
          <line x1="14" y1="8" x2="26" y2="8" stroke="#ff0000" stroke-width="8" stroke-linecap="butt"/>
          <line x1="14" y1="20" x2="26" y2="20" stroke="#ff0000" stroke-width="8" stroke-linecap="round"/>
        </svg>
        """

        let image = try await renderer.render(svgString: svg, options: .default)
        guard let cgImage = image.cgImage else {
            XCTFail("Missing CGImage")
            return
        }

        let buttOutside = try pixelAt(cgImage: cgImage, x: 11, y: 8)
        let roundOutside = try pixelAt(cgImage: cgImage, x: 11, y: 20)

        XCTAssertLessThan(buttOutside.a, 20)
        XCTAssertGreaterThan(roundOutside.a, 120)
    }

    func testStrokeDashArrayCreatesVisibleGap() async throws {
        let renderer = SVGRenderer()
        let svg = """
        <svg width="40" height="24" xmlns="http://www.w3.org/2000/svg">
          <line x1="2" y1="12" x2="38" y2="12" stroke="#ff0000" stroke-width="4" stroke-dasharray="6 4"/>
        </svg>
        """

        let image = try await renderer.render(svgString: svg, options: .default)
        guard let cgImage = image.cgImage else {
            XCTFail("Missing CGImage")
            return
        }

        let onDash = try pixelAt(cgImage: cgImage, x: 4, y: 12)
        let offDash = try pixelAt(cgImage: cgImage, x: 10, y: 12)
        let secondDash = try pixelAt(cgImage: cgImage, x: 14, y: 12)

        XCTAssertGreaterThan(onDash.a, 120)
        XCTAssertLessThan(offDash.a, 20)
        XCTAssertGreaterThan(secondDash.a, 120)
    }

    func testSkewXTransformRendersShearedGeometry() async throws {
        let renderer = SVGRenderer()
        let svg = """
        <svg width="24" height="24" xmlns="http://www.w3.org/2000/svg">
          <rect x="2" y="2" width="8" height="8" fill="#ff0000" transform="skewX(45)"/>
        </svg>
        """

        let image = try await renderer.render(svgString: svg, options: .default)
        guard let cgImage = image.cgImage else {
            XCTFail("Missing CGImage")
            return
        }

        let shearedPixel = try pixelAt(cgImage: cgImage, x: 16, y: 9)
        let unshearedArea = try pixelAt(cgImage: cgImage, x: 5, y: 9)

        XCTAssertGreaterThan(shearedPixel.a, 80)
        XCTAssertLessThan(unshearedArea.a, 20)
    }

    func testPolygonPointListParsesCommaSeparatedPairs() async throws {
        let renderer = SVGRenderer()
        let svg = """
        <svg width="20" height="20" xmlns="http://www.w3.org/2000/svg">
          <polygon points="2,2,18,2,10,18" fill="#00ff00"/>
        </svg>
        """

        let image = try await renderer.render(svgString: svg, options: .default)
        guard let cgImage = image.cgImage else {
            XCTFail("Missing CGImage")
            return
        }

        let inside = try pixelAt(cgImage: cgImage, x: 10, y: 8)
        XCTAssertGreaterThan(inside.g, 180)
        XCTAssertLessThan(inside.r, 40)
        XCTAssertLessThan(inside.b, 40)
    }

    func testUseElementRendersReferencedShapeWithOffset() async throws {
        let renderer = SVGRenderer()
        let svg = """
        <svg width="32" height="20" xmlns="http://www.w3.org/2000/svg">
          <defs>
            <circle id="dot" cx="4" cy="4" r="4" fill="#ff0000"/>
          </defs>
          <use href="#dot" x="10" y="6"/>
        </svg>
        """

        let image = try await renderer.render(svgString: svg, options: .default)
        guard let cgImage = image.cgImage else {
            XCTFail("Missing CGImage")
            return
        }

        let movedCenter = try pixelAt(cgImage: cgImage, x: 14, y: 10)
        XCTAssertGreaterThan(movedCenter.r, 200)
        XCTAssertLessThan(movedCenter.g, 40)
        XCTAssertLessThan(movedCenter.b, 40)

        let originalDefsLocation = try pixelAt(cgImage: cgImage, x: 4, y: 4)
        XCTAssertLessThan(originalDefsLocation.a, 10, "Definitions should not render directly")
    }

    func testDisplayNoneSkipsElement() async throws {
        let renderer = SVGRenderer()
        let svg = """
        <svg width="20" height="20" xmlns="http://www.w3.org/2000/svg">
          <rect x="0" y="0" width="20" height="20" fill="#ff0000" style="display:none;"/>
          <rect x="10" y="0" width="10" height="20" fill="#0000ff"/>
        </svg>
        """

        let image = try await renderer.render(svgString: svg, options: .default)
        guard let cgImage = image.cgImage else {
            XCTFail("Missing CGImage")
            return
        }

        let left = try pixelAt(cgImage: cgImage, x: 4, y: 10)
        let right = try pixelAt(cgImage: cgImage, x: 15, y: 10)

        XCTAssertLessThan(left.a, 10)
        XCTAssertGreaterThan(right.b, 180)
        XCTAssertLessThan(right.r, 40)
    }

    func testImageDataURLRendersAndPreservesOrientation() async throws {
        let sourceImage = UIGraphicsImageRenderer(size: CGSize(width: 2, height: 2)).image { ctx in
            UIColor.red.setFill()
            ctx.fill(CGRect(x: 0, y: 0, width: 2, height: 1))
            UIColor.blue.setFill()
            ctx.fill(CGRect(x: 0, y: 1, width: 2, height: 1))
        }
        guard let pngData = sourceImage.pngData() else {
            XCTFail("Failed to generate source PNG")
            return
        }
        let href = "data:image/png;base64,\(pngData.base64EncodedString())"
        let svg = """
        <svg width="2" height="2" xmlns="http://www.w3.org/2000/svg">
          <image href="\(href)" x="0" y="0" width="2" height="2"/>
        </svg>
        """

        let renderer = SVGRenderer()
        let image = try await renderer.render(svgString: svg, options: .default)
        guard let cgImage = image.cgImage else {
            XCTFail("Missing CGImage")
            return
        }

        let top = try pixelAt(cgImage: cgImage, x: 1, y: 0)
        let bottom = try pixelAt(cgImage: cgImage, x: 1, y: 1)

        XCTAssertGreaterThan(top.r, 180)
        XCTAssertLessThan(top.b, 80)
        XCTAssertGreaterThan(bottom.b, 180)
        XCTAssertLessThan(bottom.r, 80)
    }

    func testDefaultPreserveAspectRatioCentersViewBoxContent() async throws {
        let renderer = SVGRenderer()
        let svg = """
        <svg width="200" height="100" viewBox="0 0 100 100" xmlns="http://www.w3.org/2000/svg">
          <rect x="0" y="0" width="100" height="100" fill="#ff0000"/>
        </svg>
        """

        let image = try await renderer.render(svgString: svg, options: .default)
        guard let cgImage = image.cgImage else {
            XCTFail("Missing CGImage")
            return
        }

        let leftBar = try pixelAt(cgImage: cgImage, x: 10, y: 50)
        let centered = try pixelAt(cgImage: cgImage, x: 100, y: 50)
        let rightBar = try pixelAt(cgImage: cgImage, x: 190, y: 50)

        XCTAssertLessThan(leftBar.a, 10)
        XCTAssertGreaterThan(centered.r, 180)
        XCTAssertLessThan(rightBar.a, 10)
    }

    private func pixelAt(cgImage: CGImage, x: Int, y: Int) throws -> (r: UInt8, g: UInt8, b: UInt8, a: UInt8) {
        let width = cgImage.width
        let height = cgImage.height
        guard x >= 0, y >= 0, x < width, y < height else {
            throw SVGRenderError.renderFailed("Pixel out of bounds")
        }

        let bytesPerPixel = 4
        let bytesPerRow = width * bytesPerPixel
        var data = [UInt8](repeating: 0, count: width * height * bytesPerPixel)

        let colorSpace = CGColorSpaceCreateDeviceRGB()
        guard let context = CGContext(data: &data,
                                      width: width,
                                      height: height,
                                      bitsPerComponent: 8,
                                      bytesPerRow: bytesPerRow,
                                      space: colorSpace,
                                      bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue) else {
            throw SVGRenderError.renderFailed("Unable to create bitmap context")
        }

        context.draw(cgImage, in: CGRect(x: 0, y: 0, width: width, height: height))
        let index = (y * width + x) * bytesPerPixel
        return (data[index], data[index + 1], data[index + 2], data[index + 3])
    }

    private func regionHasOpaquePixels(cgImage: CGImage, x: Int, y: Int, width: Int, height: Int) throws -> Bool {
        let imageWidth = cgImage.width
        let imageHeight = cgImage.height
        guard x >= 0, y >= 0, x + width <= imageWidth, y + height <= imageHeight else {
            throw SVGRenderError.renderFailed("Region out of bounds")
        }

        let bytesPerPixel = 4
        let bytesPerRow = imageWidth * bytesPerPixel
        var data = [UInt8](repeating: 0, count: imageWidth * imageHeight * bytesPerPixel)

        let colorSpace = CGColorSpaceCreateDeviceRGB()
        guard let context = CGContext(data: &data,
                                      width: imageWidth,
                                      height: imageHeight,
                                      bitsPerComponent: 8,
                                      bytesPerRow: bytesPerRow,
                                      space: colorSpace,
                                      bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue) else {
            throw SVGRenderError.renderFailed("Unable to create bitmap context")
        }

        context.draw(cgImage, in: CGRect(x: 0, y: 0, width: imageWidth, height: imageHeight))

        for yy in y..<(y + height) {
            for xx in x..<(x + width) {
                let index = (yy * imageWidth + xx) * bytesPerPixel
                if data[index + 3] > 0 {
                    return true
                }
            }
        }
        return false
    }

    private func opaqueBounds(cgImage: CGImage) throws -> (minX: Int, minY: Int, maxX: Int, maxY: Int)? {
        let imageWidth = cgImage.width
        let imageHeight = cgImage.height
        let bytesPerPixel = 4
        let bytesPerRow = imageWidth * bytesPerPixel
        var data = [UInt8](repeating: 0, count: imageWidth * imageHeight * bytesPerPixel)

        let colorSpace = CGColorSpaceCreateDeviceRGB()
        guard let context = CGContext(data: &data,
                                      width: imageWidth,
                                      height: imageHeight,
                                      bitsPerComponent: 8,
                                      bytesPerRow: bytesPerRow,
                                      space: colorSpace,
                                      bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue) else {
            throw SVGRenderError.renderFailed("Unable to create bitmap context")
        }

        context.draw(cgImage, in: CGRect(x: 0, y: 0, width: imageWidth, height: imageHeight))

        var minX = imageWidth
        var minY = imageHeight
        var maxX = -1
        var maxY = -1

        for yy in 0..<imageHeight {
            for xx in 0..<imageWidth {
                let index = (yy * imageWidth + xx) * bytesPerPixel
                if data[index + 3] > 0 {
                    minX = min(minX, xx)
                    minY = min(minY, yy)
                    maxX = max(maxX, xx)
                    maxY = max(maxY, yy)
                }
            }
        }

        if maxX < 0 || maxY < 0 {
            return nil
        }
        return (minX, minY, maxX, maxY)
    }

    private func opaquePixelCount(cgImage: CGImage) throws -> Int {
        let imageWidth = cgImage.width
        let imageHeight = cgImage.height
        let bytesPerPixel = 4
        let bytesPerRow = imageWidth * bytesPerPixel
        var data = [UInt8](repeating: 0, count: imageWidth * imageHeight * bytesPerPixel)

        let colorSpace = CGColorSpaceCreateDeviceRGB()
        guard let context = CGContext(data: &data,
                                      width: imageWidth,
                                      height: imageHeight,
                                      bitsPerComponent: 8,
                                      bytesPerRow: bytesPerRow,
                                      space: colorSpace,
                                      bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue) else {
            throw SVGRenderError.renderFailed("Unable to create bitmap context")
        }

        context.draw(cgImage, in: CGRect(x: 0, y: 0, width: imageWidth, height: imageHeight))
        var count = 0
        for i in stride(from: 3, to: data.count, by: 4) {
            if data[i] > 0 {
                count += 1
            }
        }
        return count
    }

    private func countPixels(cgImage: CGImage,
                             x: Int,
                             y: Int,
                             width: Int,
                             height: Int,
                             where predicate: ((r: UInt8, g: UInt8, b: UInt8, a: UInt8)) -> Bool) throws -> Int {
        let imageWidth = cgImage.width
        let imageHeight = cgImage.height
        guard x >= 0, y >= 0, x + width <= imageWidth, y + height <= imageHeight else {
            throw SVGRenderError.renderFailed("Region out of bounds")
        }

        let bytesPerPixel = 4
        let bytesPerRow = imageWidth * bytesPerPixel
        var data = [UInt8](repeating: 0, count: imageWidth * imageHeight * bytesPerPixel)

        let colorSpace = CGColorSpaceCreateDeviceRGB()
        guard let context = CGContext(data: &data,
                                      width: imageWidth,
                                      height: imageHeight,
                                      bitsPerComponent: 8,
                                      bytesPerRow: bytesPerRow,
                                      space: colorSpace,
                                      bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue) else {
            throw SVGRenderError.renderFailed("Unable to create bitmap context")
        }

        context.draw(cgImage, in: CGRect(x: 0, y: 0, width: imageWidth, height: imageHeight))

        var count = 0
        for yy in y..<(y + height) {
            for xx in x..<(x + width) {
                let index = (yy * imageWidth + xx) * bytesPerPixel
                let pixel = (r: data[index], g: data[index + 1], b: data[index + 2], a: data[index + 3])
                if predicate(pixel) {
                    count += 1
                }
            }
        }
        return count
    }

    private func firstPixelMatching(cgImage: CGImage,
                                    x: Int,
                                    y: Int,
                                    width: Int,
                                    height: Int,
                                    where predicate: ((r: UInt8, g: UInt8, b: UInt8, a: UInt8)) -> Bool) throws -> (x: Int, y: Int, rgba: (UInt8, UInt8, UInt8, UInt8))? {
        let imageWidth = cgImage.width
        let imageHeight = cgImage.height
        guard x >= 0, y >= 0, x + width <= imageWidth, y + height <= imageHeight else {
            throw SVGRenderError.renderFailed("Region out of bounds")
        }

        let bytesPerPixel = 4
        let bytesPerRow = imageWidth * bytesPerPixel
        var data = [UInt8](repeating: 0, count: imageWidth * imageHeight * bytesPerPixel)

        let colorSpace = CGColorSpaceCreateDeviceRGB()
        guard let context = CGContext(data: &data,
                                      width: imageWidth,
                                      height: imageHeight,
                                      bitsPerComponent: 8,
                                      bytesPerRow: bytesPerRow,
                                      space: colorSpace,
                                      bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue) else {
            throw SVGRenderError.renderFailed("Unable to create bitmap context")
        }

        context.draw(cgImage, in: CGRect(x: 0, y: 0, width: imageWidth, height: imageHeight))

        for yy in y..<(y + height) {
            for xx in x..<(x + width) {
                let index = (yy * imageWidth + xx) * bytesPerPixel
                let pixel = (r: data[index], g: data[index + 1], b: data[index + 2], a: data[index + 3])
                if predicate(pixel) {
                    return (xx, yy, (pixel.r, pixel.g, pixel.b, pixel.a))
                }
            }
        }
        return nil
    }

    private func packageRoot() -> URL {
        let fileURL = URL(fileURLWithPath: #filePath)
        return fileURL
            .deletingLastPathComponent()
            .deletingLastPathComponent()
            .deletingLastPathComponent()
    }
}

actor MockLoader: SVGExternalResourceLoader {
    private var requests: [URL] = []

    func loadResource(_ request: SVGExternalResourceRequest) async throws -> Data {
        requests.append(request.url)
        return Data([0x89, 0x50, 0x4E, 0x47])
    }

    func callCount() -> Int {
        requests.count
    }
}
