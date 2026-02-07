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

    func testInheritedCurrentColorUpdatesWithChildColor() async throws {
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
        XCTAssertGreaterThan(right.b, 180)
        XCTAssertLessThan(right.r, 40)
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
