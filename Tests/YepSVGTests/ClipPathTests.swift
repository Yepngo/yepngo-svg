import XCTest
@testable import YepSVG

final class ClipPathTests: XCTestCase {

    func testBasicCircleClipPath() async throws {
        let svg = """
        <svg width="200" height="200" xmlns="http://www.w3.org/2000/svg">
          <defs>
            <clipPath id="myClip">
              <circle cx="100" cy="100" r="50"/>
            </clipPath>
          </defs>
          <rect x="0" y="0" width="200" height="200" fill="blue" clip-path="url(#myClip)"/>
        </svg>
        """

        let renderer = SVGRenderer()
        let image = try await renderer.render(svgString: svg, options: .default)

        // Image should be 200x200
        XCTAssertEqual(image.size.width, 200)
        XCTAssertEqual(image.size.height, 200)

        // Center should be blue (inside clip circle)
        let centerColor = pixelAt(image: image, x: 100, y: 100)
        XCTAssertNotNil(centerColor, "Center pixel should exist")
        XCTAssertEqual(centerColor?.blue, 1.0, accuracy: 0.1, "Center should be blue")
        XCTAssertEqual(centerColor?.alpha, 1.0, accuracy: 0.1, "Center should be opaque")

        // Corner should be transparent (outside clip circle)
        let cornerColor = pixelAt(image: image, x: 10, y: 10)
        XCTAssertNotNil(cornerColor, "Corner pixel should exist")
        XCTAssertEqual(cornerColor?.alpha, 0.0, accuracy: 0.1, "Corner should be transparent (clipped)")
    }

    func testRectClipPath() async throws {
        let svg = """
        <svg width="200" height="200" xmlns="http://www.w3.org/2000/svg">
          <defs>
            <clipPath id="rectClip">
              <rect x="50" y="50" width="100" height="100"/>
            </clipPath>
          </defs>
          <circle cx="100" cy="100" r="80" fill="red" clip-path="url(#rectClip)"/>
        </svg>
        """

        let renderer = SVGRenderer()
        let image = try await renderer.render(svgString: svg, options: .default)

        // Center should be red (inside both circle and clip rect)
        let centerColor = pixelAt(image: image, x: 100, y: 100)
        XCTAssertNotNil(centerColor)
        XCTAssertEqual(centerColor?.red, 1.0, accuracy: 0.1)

        // Point at (25, 100) should be transparent (outside clip rect)
        let outsideColor = pixelAt(image: image, x: 25, y: 100)
        XCTAssertNotNil(outsideColor)
        XCTAssertEqual(outsideColor?.alpha, 0.0, accuracy: 0.1, "Should be clipped")
    }

    func testPathClipPath() async throws {
        let svg = """
        <svg width="200" height="200" xmlns="http://www.w3.org/2000/svg">
          <defs>
            <clipPath id="pathClip">
              <path d="M 50,50 L 150,50 L 150,150 L 50,150 Z"/>
            </clipPath>
          </defs>
          <rect x="0" y="0" width="200" height="200" fill="green" clip-path="url(#pathClip)"/>
        </svg>
        """

        let renderer = SVGRenderer()
        let image = try await renderer.render(svgString: svg, options: .default)

        // Center should be green
        let centerColor = pixelAt(image: image, x: 100, y: 100)
        XCTAssertNotNil(centerColor)
        XCTAssertEqual(centerColor?.green, 1.0, accuracy: 0.1)

        // Corner should be clipped
        let cornerColor = pixelAt(image: image, x: 10, y: 10)
        XCTAssertNotNil(cornerColor)
        XCTAssertEqual(cornerColor?.alpha, 0.0, accuracy: 0.1)
    }

    func testObjectBoundingBoxUnits() async throws {
        let svg = """
        <svg width="200" height="200" xmlns="http://www.w3.org/2000/svg">
          <defs>
            <clipPath id="objectClip" clipPathUnits="objectBoundingBox">
              <rect x="0.25" y="0.25" width="0.5" height="0.5"/>
            </clipPath>
          </defs>
          <rect x="50" y="50" width="100" height="100" fill="red" clip-path="url(#objectClip)"/>
        </svg>
        """

        let renderer = SVGRenderer()
        let image = try await renderer.render(svgString: svg, options: .default)

        // Center of the rect should be red (inside clip)
        let centerColor = pixelAt(image: image, x: 100, y: 100)
        XCTAssertNotNil(centerColor)
        XCTAssertEqual(centerColor?.red, 1.0, accuracy: 0.1)

        // Top-left of rect should be clipped (outside the 0.25-0.75 range)
        let topLeftColor = pixelAt(image: image, x: 55, y: 55)
        XCTAssertNotNil(topLeftColor)
        XCTAssertLessThan(topLeftColor?.alpha ?? 1, 0.1, "Should be clipped")
    }

    func testEvenOddClipRule() async throws {
        // Test with self-intersecting path - evenodd should create hole
        let svg = """
        <svg width="200" height="200" xmlns="http://www.w3.org/2000/svg">
          <defs>
            <clipPath id="evenoddClip">
              <path clip-rule="evenodd" d="M50,50 L150,50 L150,150 L50,150 Z M75,75 L125,75 L125,125 L75,125 Z"/>
            </clipPath>
          </defs>
          <rect x="0" y="0" width="200" height="200" fill="blue" clip-path="url(#evenoddClip)"/>
        </svg>
        """

        let renderer = SVGRenderer()
        let image = try await renderer.render(svgString: svg, options: .default)

        // Outer ring should be blue (between outer and inner squares)
        let outerColor = pixelAt(image: image, x: 60, y: 100)
        XCTAssertNotNil(outerColor)
        XCTAssertGreaterThan(outerColor?.blue ?? 0, 0.5, "Outer ring should be visible")

        // Inner square should be clipped (even-odd rule creates hole)
        let innerColor = pixelAt(image: image, x: 100, y: 100)
        XCTAssertNotNil(innerColor)
        XCTAssertLessThan(innerColor?.alpha ?? 1, 0.1, "Inner should be clipped with evenodd")
    }

    func testNonzeroClipRule() async throws {
        // Same path as evenodd test but with nonzero - should be solid
        let svg = """
        <svg width="200" height="200" xmlns="http://www.w3.org/2000/svg">
          <defs>
            <clipPath id="nonzeroClip">
              <path clip-rule="nonzero" d="M50,50 L150,50 L150,150 L50,150 Z M75,75 L125,75 L125,125 L75,125 Z"/>
            </clipPath>
          </defs>
          <rect x="0" y="0" width="200" height="200" fill="red" clip-path="url(#nonzeroClip)"/>
        </svg>
        """

        let renderer = SVGRenderer()
        let image = try await renderer.render(svgString: svg, options: .default)

        // Center should be red (nonzero doesn't create hole)
        let centerColor = pixelAt(image: image, x: 100, y: 100)
        XCTAssertNotNil(centerColor)
        XCTAssertGreaterThan(centerColor?.red ?? 0, 0.5, "Center should be visible with nonzero")
        XCTAssertGreaterThan(centerColor?.alpha ?? 0, 0.5, "Center should not be clipped")
    }

    func testMultipleShapesInClipPath() async throws {
        let svg = """
        <svg width="200" height="200" xmlns="http://www.w3.org/2000/svg">
          <defs>
            <clipPath id="multiClip">
              <circle cx="60" cy="100" r="30"/>
              <circle cx="140" cy="100" r="30"/>
            </clipPath>
          </defs>
          <rect x="0" y="0" width="200" height="200" fill="purple" clip-path="url(#multiClip)"/>
        </svg>
        """

        let renderer = SVGRenderer()
        let image = try await renderer.render(svgString: svg, options: .default)

        // Left circle center should be purple
        let leftColor = pixelAt(image: image, x: 60, y: 100)
        XCTAssertNotNil(leftColor)
        XCTAssertGreaterThan(leftColor?.alpha ?? 0, 0.5, "Left circle should be visible")

        // Right circle center should be purple
        let rightColor = pixelAt(image: image, x: 140, y: 100)
        XCTAssertNotNil(rightColor)
        XCTAssertGreaterThan(rightColor?.alpha ?? 0, 0.5, "Right circle should be visible")

        // Center should be clipped
        let centerColor = pixelAt(image: image, x: 100, y: 100)
        XCTAssertNotNil(centerColor)
        XCTAssertLessThan(centerColor?.alpha ?? 1, 0.1, "Center should be clipped")
    }
}

// Helper function to extract pixel color
private func pixelAt(image: UIImage, x: Int, y: Int) -> (red: CGFloat, green: CGFloat, blue: CGFloat, alpha: CGFloat)? {
    guard let cgImage = image.cgImage else { return nil }
    guard let dataProvider = cgImage.dataProvider else { return nil }
    guard let pixelData = dataProvider.data else { return nil }

    let data = CFDataGetBytePtr(pixelData)
    let bytesPerRow = cgImage.bytesPerRow
    let bytesPerPixel = cgImage.bitsPerPixel / 8
    let pixelOffset = y * bytesPerRow + x * bytesPerPixel

    let r = CGFloat(data![pixelOffset]) / 255.0
    let g = CGFloat(data![pixelOffset + 1]) / 255.0
    let b = CGFloat(data![pixelOffset + 2]) / 255.0
    let a = CGFloat(data![pixelOffset + 3]) / 255.0

    return (r, g, b, a)
}
