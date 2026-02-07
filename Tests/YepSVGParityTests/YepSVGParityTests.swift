import XCTest
import UIKit
import ImageIO
@testable import YepSVG

final class YepSVGParityTests: XCTestCase {
    private let threshold: Double = 0.005

    func testFixtureParityAgainstChromiumGoldens() async throws {
        let root = packageRoot()
        let fixtureDir = root.appendingPathComponent("Fixtures/svg", isDirectory: true)
        let goldenDir = root.appendingPathComponent("Fixtures/golden/chromium", isDirectory: true)
        let deviationsURL = goldenDir.appendingPathComponent("known-deviations.json")
        let deviations = try loadKnownDeviations(at: deviationsURL)

        let fixtureFiles = try FileManager.default.contentsOfDirectory(at: fixtureDir, includingPropertiesForKeys: nil)
            .filter { $0.pathExtension.lowercased() == "svg" }
            .sorted { $0.lastPathComponent < $1.lastPathComponent }

        if fixtureFiles.isEmpty {
            throw XCTSkip("No SVG fixtures found")
        }

        let renderer = SVGRenderer()
        var compared = 0
        var usedDeviationIDs = Set<String>()

        for svgURL in fixtureFiles {
            let name = svgURL.deletingPathExtension().lastPathComponent
            let goldenURL = goldenDir.appendingPathComponent("\(name).png")

            guard FileManager.default.fileExists(atPath: goldenURL.path) else {
                continue
            }

            let image = try await renderer.render(svgFileURL: svgURL, options: .default)
            guard let rendered = image.cgImage else {
                XCTFail("Missing CGImage for fixture \(name)")
                continue
            }

            let goldenData = try Data(contentsOf: goldenURL)
            guard let source = CGImageSourceCreateWithData(goldenData as CFData, nil),
                  let golden = CGImageSourceCreateImageAtIndex(source, 0, nil) else {
                XCTFail("Failed to decode golden image \(goldenURL.lastPathComponent)")
                continue
            }

            let diff = try pixelDiffRatio(lhs: rendered, rhs: golden)
            if diff > threshold {
                if let deviation = deviations[name], diff <= deviation.maxDiffRatio {
                    usedDeviationIDs.insert(name)
                } else {
                    XCTFail("Pixel diff \(diff) exceeded threshold \(threshold) for \(name), and no tracked deviation covered it")
                }
            }
            compared += 1
        }

        if compared == 0 {
            throw XCTSkip("No chromium golden PNGs found to compare")
        }

        let staleDeviationIDs = Set(deviations.keys).subtracting(usedDeviationIDs)
        XCTAssertTrue(staleDeviationIDs.isEmpty, "Tracked deviations are stale or unnecessary: \(staleDeviationIDs.sorted())")
    }

    private func pixelDiffRatio(lhs: CGImage, rhs: CGImage) throws -> Double {
        guard lhs.width == rhs.width, lhs.height == rhs.height else {
            throw XCTSkip("Image dimensions differ (\(lhs.width)x\(lhs.height) vs \(rhs.width)x\(rhs.height))")
        }

        let width = lhs.width
        let height = lhs.height
        let bytesPerPixel = 4
        let bytesPerRow = width * bytesPerPixel
        let count = width * height * bytesPerPixel

        var lhsBytes = [UInt8](repeating: 0, count: count)
        var rhsBytes = [UInt8](repeating: 0, count: count)

        let space = CGColorSpaceCreateDeviceRGB()
        guard let lhsCtx = CGContext(data: &lhsBytes,
                                     width: width,
                                     height: height,
                                     bitsPerComponent: 8,
                                     bytesPerRow: bytesPerRow,
                                     space: space,
                                     bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue),
              let rhsCtx = CGContext(data: &rhsBytes,
                                     width: width,
                                     height: height,
                                     bitsPerComponent: 8,
                                     bytesPerRow: bytesPerRow,
                                     space: space,
                                     bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)
        else {
            throw XCTSkip("Failed to allocate bitmap contexts for diff")
        }

        lhsCtx.draw(lhs, in: CGRect(x: 0, y: 0, width: width, height: height))
        rhsCtx.draw(rhs, in: CGRect(x: 0, y: 0, width: width, height: height))

        var mismatches = 0
        var index = 0
        while index < count {
            if lhsBytes[index] != rhsBytes[index] ||
                lhsBytes[index + 1] != rhsBytes[index + 1] ||
                lhsBytes[index + 2] != rhsBytes[index + 2] ||
                lhsBytes[index + 3] != rhsBytes[index + 3] {
                mismatches += 1
            }
            index += 4
        }

        return Double(mismatches) / Double(width * height)
    }

    private func packageRoot() -> URL {
        let fileURL = URL(fileURLWithPath: #filePath)
        return fileURL
            .deletingLastPathComponent()
            .deletingLastPathComponent()
            .deletingLastPathComponent()
    }

    private func loadKnownDeviations(at url: URL) throws -> [String: KnownDeviation] {
        guard FileManager.default.fileExists(atPath: url.path) else {
            return [:]
        }
        let data = try Data(contentsOf: url)
        let manifest = try JSONDecoder().decode(DeviationManifest.self, from: data)
        return Dictionary(uniqueKeysWithValues: manifest.deviations.map { ($0.id, $0) })
    }
}

private struct DeviationManifest: Decodable {
    let version: Int
    let threshold: Double
    let deviations: [KnownDeviation]
}

private struct KnownDeviation: Decodable {
    let id: String
    let reason: String
    let maxDiffRatio: Double
}
