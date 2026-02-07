import SwiftUI
import UIKit
import CoreGraphics
import YepSVG

private struct W3CTestCase: Identifiable, Hashable {
    let id: String
    let svgURL: URL
    let referencePNGURL: URL?
}

@MainActor
private final class W3CTestHarnessModel: ObservableObject {
    @Published var suiteRootURL: URL?
    @Published var allTests: [W3CTestCase] = []
    @Published var filteredTests: [W3CTestCase] = []
    @Published var selectedTestID: String?
    @Published var query: String = "" {
        didSet { applyFilter() }
    }

    @Published var scale: Double = 1.0
    @Published var isRendering: Bool = false
    @Published var renderedImage: UIImage?
    @Published var referenceImage: UIImage?
    @Published var diffRatio: Double?
    @Published var errorMessage: String?

    private let renderer = SVGRenderer()
    private let diffThreshold = 0.005

    var selectedTest: W3CTestCase? {
        guard let selectedTestID else { return nil }
        return allTests.first { $0.id == selectedTestID }
    }

    var hasSuite: Bool {
        suiteRootURL != nil
    }

    var diffSummary: String {
        guard let diffRatio else { return "Diff: n/a" }
        let percent = diffRatio * 100.0
        let thresholdPercent = diffThreshold * 100.0
        if diffRatio <= diffThreshold {
            return String(format: "Diff: %.3f%% (PASS, threshold %.3f%%)", percent, thresholdPercent)
        }
        return String(format: "Diff: %.3f%% (FAIL, threshold %.3f%%)", percent, thresholdPercent)
    }

    func loadSuiteFromBundle() {
        errorMessage = nil

        let candidates: [URL?] = [
            Bundle.main.resourceURL?.appendingPathComponent("W3CSuite", isDirectory: true),
            Bundle.main.resourceURL?.appendingPathComponent("Resources/W3CSuite", isDirectory: true),
            Bundle.main.resourceURL
        ]

        guard let root = candidates.compactMap({ $0 }).first(where: { candidate in
            let svgDir = candidate.appendingPathComponent("svggen", isDirectory: true)
            let pngDir = candidate.appendingPathComponent("png", isDirectory: true)
            return FileManager.default.fileExists(atPath: svgDir.path) &&
                FileManager.default.fileExists(atPath: pngDir.path)
        }) else {
            suiteRootURL = nil
            allTests = []
            filteredTests = []
            selectedTestID = nil
            errorMessage = "W3C suite not bundled. Add `YepSVGSampleApp/Resources/W3CSuite` with `svggen/` and `png/`."
            return
        }

        suiteRootURL = root
        let svgDir = root.appendingPathComponent("svggen", isDirectory: true)
        let pngDir = root.appendingPathComponent("png", isDirectory: true)

        do {
            let svgFiles = try FileManager.default.contentsOfDirectory(at: svgDir, includingPropertiesForKeys: nil)
                .filter { $0.pathExtension.lowercased() == "svg" }
                .sorted { $0.lastPathComponent < $1.lastPathComponent }

            allTests = svgFiles.map { svgURL in
                let name = svgURL.deletingPathExtension().lastPathComponent
                let pngURL = pngDir.appendingPathComponent("full-\(name).png")
                let reference = FileManager.default.fileExists(atPath: pngURL.path) ? pngURL : nil
                return W3CTestCase(id: name, svgURL: svgURL, referencePNGURL: reference)
            }
            applyFilter()

            if selectedTestID == nil {
                selectedTestID = filteredTests.first?.id
            }
        } catch {
            errorMessage = "Failed to load suite: \(error.localizedDescription)"
            allTests = []
            filteredTests = []
            selectedTestID = nil
        }
    }

    func renderSelected() async {
        guard let test = selectedTest else {
            renderedImage = nil
            referenceImage = nil
            diffRatio = nil
            return
        }

        isRendering = true
        errorMessage = nil
        defer { isRendering = false }

        do {
            let data = try Data(contentsOf: test.svgURL)
            var options = SVGRenderOptions.default
            options.scale = scale
            options.backgroundColor = UIColor.clear.cgColor
            renderedImage = try await renderer.render(svgData: data, options: options)

            if let referenceURL = test.referencePNGURL {
                referenceImage = UIImage(contentsOfFile: referenceURL.path)
            } else {
                referenceImage = nil
            }

            if let renderedCG = renderedImage?.cgImage,
               let referenceCG = referenceImage?.cgImage {
                diffRatio = pixelDiffRatio(lhs: renderedCG, rhs: referenceCG)
            } else {
                diffRatio = nil
            }
        } catch {
            renderedImage = nil
            referenceImage = nil
            diffRatio = nil
            errorMessage = "Render failed: \(error.localizedDescription)"
        }
    }

    func selectTest(_ test: W3CTestCase) {
        selectedTestID = test.id
    }

    private func applyFilter() {
        let needle = query.trimmingCharacters(in: .whitespacesAndNewlines).lowercased()
        if needle.isEmpty {
            filteredTests = allTests
        } else {
            filteredTests = allTests.filter { $0.id.lowercased().contains(needle) }
        }

        if let selectedTestID,
           !filteredTests.contains(where: { $0.id == selectedTestID }) {
            self.selectedTestID = filteredTests.first?.id
        }
    }

    private func pixelDiffRatio(lhs: CGImage, rhs: CGImage) -> Double {
        guard lhs.width == rhs.width, lhs.height == rhs.height else {
            return 1.0
        }

        let width = lhs.width
        let height = lhs.height
        let bytesPerPixel = 4
        let byteCount = width * height * bytesPerPixel
        let bytesPerRow = width * bytesPerPixel

        var lhsBytes = [UInt8](repeating: 0, count: byteCount)
        var rhsBytes = [UInt8](repeating: 0, count: byteCount)

        let colorSpace = CGColorSpaceCreateDeviceRGB()
        guard let lhsContext = CGContext(data: &lhsBytes,
                                         width: width,
                                         height: height,
                                         bitsPerComponent: 8,
                                         bytesPerRow: bytesPerRow,
                                         space: colorSpace,
                                         bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue),
              let rhsContext = CGContext(data: &rhsBytes,
                                         width: width,
                                         height: height,
                                         bitsPerComponent: 8,
                                         bytesPerRow: bytesPerRow,
                                         space: colorSpace,
                                         bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue) else {
            return 1.0
        }

        lhsContext.draw(lhs, in: CGRect(x: 0, y: 0, width: width, height: height))
        rhsContext.draw(rhs, in: CGRect(x: 0, y: 0, width: width, height: height))

        var mismatches = 0
        var index = 0
        while index < byteCount {
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
}

struct W3CTestHarnessView: View {
    @StateObject private var model = W3CTestHarnessModel()

    var body: some View {
        NavigationView {
            VStack(alignment: .leading, spacing: 12) {
                if !model.hasSuite {
                    Text("W3C SVG 1.1 Harness")
                        .font(.title2.weight(.semibold))
                    Text(model.errorMessage ?? "Suite not available")
                        .font(.footnote)
                        .foregroundStyle(.secondary)
                    Text("Bundle `Resources/W3CSuite` from W3C full suite archive.")
                        .font(.footnote)
                        .foregroundStyle(.secondary)
                    Spacer()
                } else {
                    TextField("Filter tests (e.g. shapes-circle)", text: $model.query)
                        .textFieldStyle(.roundedBorder)

                    HStack {
                        Text("\(model.filteredTests.count) tests")
                            .font(.footnote)
                            .foregroundStyle(.secondary)
                        Spacer()
                        if let selected = model.selectedTest {
                            Text(selected.id)
                                .font(.footnote.monospaced())
                                .foregroundStyle(.secondary)
                        }
                    }

                    List(model.filteredTests) { test in
                        Button {
                            model.selectTest(test)
                            Task { await model.renderSelected() }
                        } label: {
                            HStack {
                                Text(test.id)
                                    .font(.system(.body, design: .monospaced))
                                Spacer()
                                if test.referencePNGURL != nil {
                                    Image(systemName: "photo")
                                        .foregroundStyle(.secondary)
                                }
                            }
                        }
                        .buttonStyle(.plain)
                    }
                    .frame(height: 240)

                    VStack(alignment: .leading, spacing: 8) {
                        HStack {
                            Text("Scale")
                            Spacer()
                            Text(String(format: "%.2fx", model.scale))
                                .foregroundStyle(.secondary)
                        }
                        Slider(value: $model.scale, in: 0.5...2.0, step: 0.1)
                    }

                    Button {
                        Task { await model.renderSelected() }
                    } label: {
                        Text("Render Selected Test")
                            .frame(maxWidth: .infinity)
                    }
                    .buttonStyle(.borderedProminent)

                    if model.isRendering {
                        ProgressView("Rendering…")
                            .frame(maxWidth: .infinity, alignment: .center)
                    }

                    if let rendered = model.renderedImage {
                        ScrollView(.horizontal) {
                            HStack(alignment: .top, spacing: 12) {
                                VStack {
                                    Text("YepSVG")
                                        .font(.caption)
                                        .foregroundStyle(.secondary)
                                    Image(uiImage: rendered)
                                        .resizable()
                                        .scaledToFit()
                                        .frame(width: 280, height: 220)
                                        .background(Color.white)
                                        .clipShape(RoundedRectangle(cornerRadius: 10, style: .continuous))
                                }

                                if let reference = model.referenceImage {
                                    VStack {
                                        Text("W3C PNG")
                                            .font(.caption)
                                            .foregroundStyle(.secondary)
                                        Image(uiImage: reference)
                                            .resizable()
                                            .scaledToFit()
                                            .frame(width: 280, height: 220)
                                            .background(Color.white)
                                            .clipShape(RoundedRectangle(cornerRadius: 10, style: .continuous))
                                    }
                                }
                            }
                            .padding(.vertical, 4)
                        }
                    }

                    if model.referenceImage != nil {
                        Text(model.diffSummary)
                            .font(.footnote)
                            .foregroundStyle(.secondary)
                    }

                    if let errorMessage = model.errorMessage {
                        Text(errorMessage)
                            .font(.footnote)
                            .foregroundStyle(.red)
                    }

                    Spacer(minLength: 0)
                }
            }
            .padding()
            .navigationTitle("W3C Harness")
            .task {
                model.loadSuiteFromBundle()
                await model.renderSelected()
            }
        }
    }
}
