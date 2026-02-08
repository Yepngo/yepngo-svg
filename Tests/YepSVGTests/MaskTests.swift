import XCTest
@testable import YepSVG

final class MaskTests: XCTestCase {

    func testBasicCircleMask() async throws {
        let svg = """
        <svg width="200" height="200" xmlns="http://www.w3.org/2000/svg">
          <defs>
            <mask id="myMask">
              <circle cx="100" cy="100" r="50" fill="white"/>
            </mask>
          </defs>
          <rect x="0" y="0" width="200" height="200" fill="blue" mask="url(#myMask)"/>
        </svg>
        """

        let renderer = SVGRenderer()
        let image = try await renderer.render(svgString: svg, options: .default)

        // Image should be 200x200
        XCTAssertEqual(image.size.width, 200)
        XCTAssertEqual(image.size.height, 200)

        // Center should be blue (inside white mask circle)
        let centerColor = pixelAt(image: image, x: 100, y: 100)
        XCTAssertNotNil(centerColor, "Center pixel should exist")
        XCTAssertGreaterThan(centerColor?.blue ?? 0, 0.5, "Center should be blue (masked visible)")
        XCTAssertGreaterThan(centerColor?.alpha ?? 0, 0.5, "Center should be visible")

        // Corner should be transparent (black in mask = invisible)
        let cornerColor = pixelAt(image: image, x: 10, y: 10)
        XCTAssertNotNil(cornerColor, "Corner pixel should exist")
        XCTAssertLessThan(cornerColor?.alpha ?? 1, 0.1, "Corner should be transparent (masked out)")
    }

    func testGradientMask() async throws {
        let svg = """
        <svg width="200" height="200" xmlns="http://www.w3.org/2000/svg">
          <defs>
            <linearGradient id="grad" x1="0%" y1="0%" x2="100%" y2="0%">
              <stop offset="0%" style="stop-color:black;stop-opacity:1" />
              <stop offset="100%" style="stop-color:white;stop-opacity:1" />
            </linearGradient>
            <mask id="gradMask">
              <rect x="0" y="0" width="200" height="200" fill="url(#grad)"/>
            </mask>
          </defs>
          <rect x="0" y="0" width="200" height="200" fill="red" mask="url(#gradMask)"/>
        </svg>
        """

        let renderer = SVGRenderer()
        let image = try await renderer.render(svgString: svg, options: .default)

        // Left edge should be less visible (darker in gradient mask)
        let leftColor = pixelAt(image: image, x: 10, y: 100)
        XCTAssertNotNil(leftColor)

        // Right edge should be more visible (lighter in gradient mask)
        let rightColor = pixelAt(image: image, x: 190, y: 100)
        XCTAssertNotNil(rightColor)

        // Right should be more opaque than left (gradient effect)
        XCTAssertGreaterThan(rightColor?.alpha ?? 0, leftColor?.alpha ?? 1)
    }

    func testMultipleShapesInMask() async throws {
        let svg = """
        <svg width="200" height="200" xmlns="http://www.w3.org/2000/svg">
          <defs>
            <mask id="multiMask">
              <circle cx="60" cy="100" r="30" fill="white"/>
              <circle cx="140" cy="100" r="30" fill="white"/>
            </mask>
          </defs>
          <rect x="0" y="0" width="200" height="200" fill="green" mask="url(#multiMask)"/>
        </svg>
        """

        let renderer = SVGRenderer()
        let image = try await renderer.render(svgString: svg, options: .default)

        // Left circle center should be visible
        let leftColor = pixelAt(image: image, x: 60, y: 100)
        XCTAssertNotNil(leftColor)
        XCTAssertGreaterThan(leftColor?.alpha ?? 0, 0.5, "Left circle should be visible")

        // Right circle center should be visible
        let rightColor = pixelAt(image: image, x: 140, y: 100)
        XCTAssertNotNil(rightColor)
        XCTAssertGreaterThan(rightColor?.alpha ?? 0, 0.5, "Right circle should be visible")

        // Center should be less visible (between circles)
        let centerColor = pixelAt(image: image, x: 100, y: 100)
        XCTAssertNotNil(centerColor)
        XCTAssertLessThan(centerColor?.alpha ?? 1, 0.5, "Center should be masked")
    }

    func testMaskWithObjectBoundingBox() async throws {
        let svg = """
        <svg width="200" height="200" xmlns="http://www.w3.org/2000/svg">
          <defs>
            <mask id="objMask" maskUnits="objectBoundingBox" maskContentUnits="objectBoundingBox">
              <rect x="0.25" y="0.25" width="0.5" height="0.5" fill="white"/>
            </mask>
          </defs>
          <rect x="50" y="50" width="100" height="100" fill="red" mask="url(#objMask)"/>
        </svg>
        """

        let renderer = SVGRenderer()
        let image = try await renderer.render(svgString: svg, options: .default)

        // Center of rect should be visible (inside 0.25-0.75 range)
        let centerColor = pixelAt(image: image, x: 100, y: 100)
        XCTAssertNotNil(centerColor)
        XCTAssertGreaterThan(centerColor?.alpha ?? 0, 0.5, "Center should be visible")

        // Corner of rect should be masked (outside 0.25-0.75 range)
        let cornerColor = pixelAt(image: image, x: 55, y: 55)
        XCTAssertNotNil(cornerColor)
        XCTAssertLessThan(cornerColor?.alpha ?? 1, 0.5, "Corner should be masked")
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
