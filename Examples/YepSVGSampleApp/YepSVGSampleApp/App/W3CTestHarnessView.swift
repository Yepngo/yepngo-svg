import SwiftUI
import UIKit
import CoreGraphics
import ImageIO
import YepSVG

private struct W3CTestCase: Identifiable, Hashable {
    let id: String
    let groupID: String
    let svgURL: URL
    let referencePNGURL: URL?
}

@MainActor
private final class W3CTestHarnessModel: ObservableObject {
    @Published var suiteRootURL: URL?
    @Published var allTests: [W3CTestCase] = []
    @Published var filteredTests: [W3CTestCase] = []
    @Published var selectedGroupID: String = "all" {
        didSet { applyFilter(selectFirstResult: true) }
    }
    @Published var selectedTestID: String?
    @Published var query: String = "" {
        didSet { applyFilter(selectFirstResult: false) }
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

    var availableGroupIDs: [String] {
        let groups = Set(allTests.map(\.groupID)).sorted(by: Self.compareGroupIDs)
        return ["all"] + groups
    }

    var groupedFilteredTests: [(groupID: String, tests: [W3CTestCase])] {
        let grouped = Dictionary(grouping: filteredTests, by: \.groupID)
        let sortedKeys = grouped.keys.sorted(by: Self.compareGroupIDs)
        return sortedKeys.map { key in
            let tests = (grouped[key] ?? []).sorted { $0.id < $1.id }
            return (groupID: key, tests: tests)
        }
    }

    var selectedIndex: Int? {
        guard let selectedTestID else { return nil }
        return filteredTests.firstIndex(where: { $0.id == selectedTestID })
    }

    var selectedPositionSummary: String {
        guard let index = selectedIndex else {
            return "0 / \(filteredTests.count)"
        }
        return "\(index + 1) / \(filteredTests.count)"
    }

    var canSelectPrevious: Bool {
        guard let index = selectedIndex else { return false }
        return index > 0
    }

    var canSelectNext: Bool {
        guard let index = selectedIndex else { return false }
        return index + 1 < filteredTests.count
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
                return W3CTestCase(id: name, groupID: Self.groupIdentifier(for: name), svgURL: svgURL, referencePNGURL: reference)
            }
            if !availableGroupIDs.contains(selectedGroupID) {
                selectedGroupID = "all"
            }
            applyFilter(selectFirstResult: false)

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
            let loadedReference: UIImage?
            if let referenceURL = test.referencePNGURL {
                loadedReference = loadReferenceImage(at: referenceURL)
            } else {
                loadedReference = nil
            }
            referenceImage = loadedReference

            var options = SVGRenderOptions.default
            options.scale = scale
            options.defaultFontSize = 12
            options.backgroundColor = UIColor.clear.cgColor
            if let referenceSize = loadedReference?.cgImage.map({ CGSize(width: $0.width, height: $0.height) }) {
                options.viewportSize = referenceSize
            }

            let rendered = try await renderer.render(svgFileURL: test.svgURL, options: options)
            renderedImage = normalizeOrientation(rendered)

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

    private func loadReferenceImage(at url: URL) -> UIImage? {
        guard let source = CGImageSourceCreateWithURL(url as CFURL, nil),
              let image = CGImageSourceCreateImageAtIndex(source, 0, nil) else {
            return nil
        }
        return normalizeOrientation(UIImage(cgImage: image))
    }

    private func normalizeOrientation(_ image: UIImage) -> UIImage {
        guard image.imageOrientation != .up else { return image }
        let format = UIGraphicsImageRendererFormat()
        format.scale = image.scale
        format.opaque = false
        let renderer = UIGraphicsImageRenderer(size: image.size, format: format)
        return renderer.image { _ in
            image.draw(in: CGRect(origin: .zero, size: image.size))
        }
    }

    func selectTest(_ test: W3CTestCase) {
        selectedTestID = test.id
    }

    func selectPrevious() {
        guard let index = selectedIndex, index > 0 else { return }
        selectedTestID = filteredTests[index - 1].id
    }

    func selectNext() {
        guard let index = selectedIndex, index + 1 < filteredTests.count else { return }
        selectedTestID = filteredTests[index + 1].id
    }

    func selectRandom() {
        guard !filteredTests.isEmpty else { return }
        selectedTestID = filteredTests.randomElement()?.id
    }

    private func applyFilter(selectFirstResult: Bool) {
        let needle = query.trimmingCharacters(in: .whitespacesAndNewlines).lowercased()
        filteredTests = allTests.filter { test in
            let groupMatches = selectedGroupID == "all" || test.groupID == selectedGroupID
            let queryMatches = needle.isEmpty || test.id.lowercased().contains(needle)
            return groupMatches && queryMatches
        }

        if selectFirstResult {
            self.selectedTestID = filteredTests.first?.id
            return
        }

        if let selectedTestID,
           !filteredTests.contains(where: { $0.id == selectedTestID }) {
            self.selectedTestID = filteredTests.first?.id
        }
    }

    static func groupIdentifier(for testID: String) -> String {
        let parts = testID.split(separator: "-", omittingEmptySubsequences: true)
        if parts.count >= 2 {
            return "\(parts[0])-\(parts[1])"
        }
        return parts.first.map(String.init) ?? "misc"
    }

    private static func compareGroupIDs(_ lhs: String, _ rhs: String) -> Bool {
        let lhsRank = groupSortRank(lhs)
        let rhsRank = groupSortRank(rhs)
        if lhsRank != rhsRank {
            return lhsRank < rhsRank
        }
        return lhs < rhs
    }

    private static func groupSortRank(_ groupID: String) -> Int {
        let lower = groupID.lowercased()
        if lower.hasPrefix("animate") || lower.contains("animation") {
            return 1
        }
        return 0
    }

    func groupDisplayName(for groupID: String) -> String {
        if groupID == "all" {
            return "All"
        }
        return groupID.replacingOccurrences(of: "-", with: " ").capitalized
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
    @State private var isBrowserPresented: Bool = false

    var body: some View {
        NavigationView {
            ScrollView {
                VStack(alignment: .leading, spacing: 12) {
                    if !model.hasSuite {
                        Text(model.errorMessage ?? "Suite not available")
                            .font(.footnote)
                            .foregroundStyle(.secondary)
                        Text("Bundle `Resources/W3CSuite` from W3C full suite archive.")
                            .font(.footnote)
                            .foregroundStyle(.secondary)
                    } else {
                        TextField("Filter tests (e.g. shapes-circle)", text: $model.query)
                            .textFieldStyle(.roundedBorder)

                        Picker("Category", selection: $model.selectedGroupID) {
                            ForEach(model.availableGroupIDs, id: \.self) { groupID in
                                Text(model.groupDisplayName(for: groupID)).tag(groupID)
                            }
                        }
                        .pickerStyle(.menu)

                        HStack {
                            Text("\(model.filteredTests.count) tests")
                                .font(.footnote)
                                .foregroundStyle(.secondary)
                            Spacer()
                            Text(model.selectedPositionSummary)
                                .font(.footnote.monospaced())
                                .foregroundStyle(.secondary)
                        }

                        VStack(alignment: .leading, spacing: 8) {
                            HStack(spacing: 10) {
                                Button {
                                    model.selectPrevious()
                                } label: {
                                    Label("Previous", systemImage: "chevron.left")
                                }
                                .buttonStyle(.bordered)
                                .disabled(!model.canSelectPrevious)

                                Button {
                                    model.selectNext()
                                } label: {
                                    Label("Next", systemImage: "chevron.right")
                                }
                                .buttonStyle(.bordered)
                                .disabled(!model.canSelectNext)

                                Spacer()

                                Button {
                                    model.selectRandom()
                                } label: {
                                    Label("Random", systemImage: "shuffle")
                                }
                                .buttonStyle(.bordered)
                            }

                            HStack(spacing: 10) {
                                if let selected = model.selectedTest {
                                    Text(selected.id)
                                        .font(.system(.footnote, design: .monospaced))
                                        .foregroundStyle(.secondary)
                                        .lineLimit(1)
                                } else {
                                    Text("No test selected")
                                        .font(.footnote)
                                        .foregroundStyle(.secondary)
                                }
                                Spacer()
                                Button {
                                    isBrowserPresented = true
                                } label: {
                                    Label("Browse Tests", systemImage: "list.bullet")
                                }
                                .buttonStyle(.bordered)
                            }
                        }

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
                            VStack(alignment: .leading, spacing: 12) {
                                VStack(alignment: .leading, spacing: 4) {
                                    Text("YepSVG")
                                        .font(.caption)
                                        .foregroundStyle(.secondary)
                                    Image(uiImage: rendered)
                                        .resizable()
                                        .scaledToFit()
                                        .frame(maxWidth: .infinity)
                                        .frame(height: 220)
                                        .background(Color.white)
                                        .clipShape(RoundedRectangle(cornerRadius: 10, style: .continuous))
                                }

                                if let reference = model.referenceImage {
                                    VStack(alignment: .leading, spacing: 4) {
                                        Text("W3C PNG")
                                            .font(.caption)
                                            .foregroundStyle(.secondary)
                                        Image(uiImage: reference)
                                            .resizable()
                                            .scaledToFit()
                                            .frame(maxWidth: .infinity)
                                            .frame(height: 220)
                                            .background(Color.white)
                                            .clipShape(RoundedRectangle(cornerRadius: 10, style: .continuous))
                                    }
                                }
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
                    }
                }
                .padding()
            }
            .navigationBarTitleDisplayMode(.inline)
            .task {
                model.loadSuiteFromBundle()
                await model.renderSelected()
            }
            .onChange(of: model.selectedTestID) { _ in
                Task { await model.renderSelected() }
            }
            .sheet(isPresented: $isBrowserPresented) {
                NavigationView {
                    List {
                        ForEach(model.groupedFilteredTests, id: \.groupID) { group in
                            Section(model.groupDisplayName(for: group.groupID)) {
                                ForEach(group.tests) { test in
                                    Button {
                                        model.selectTest(test)
                                        isBrowserPresented = false
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
                            }
                        }
                    }
                    .navigationTitle("Choose Test")
                    .toolbar {
                        ToolbarItem(placement: .cancellationAction) {
                            Button("Done") { isBrowserPresented = false }
                        }
                    }
                }
            }
        }
    }
}
