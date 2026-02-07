import SwiftUI
import UIKit
import YepSVG

struct ContentView: View {
    private enum SampleKind: String, CaseIterable, Identifiable {
        case basic = "Basic"
        case complex = "Complex"

        var id: String { rawValue }
    }

    @State private var selectedSample: SampleKind = .complex
    @State private var scale: Double = 1.0
    @State private var isRendering = false
    @State private var renderedImage: UIImage?
    @State private var errorMessage: String?

    private let renderer = SVGRenderer()

    var body: some View {
        NavigationView {
            ScrollView {
                VStack(spacing: 20) {
                    Picker("SVG Sample", selection: $selectedSample) {
                        ForEach(SampleKind.allCases) { sample in
                            Text(sample.rawValue).tag(sample)
                        }
                    }
                    .pickerStyle(.segmented)
                    .onChange(of: selectedSample) { _ in
                        Task { await renderBadge() }
                    }

                    Group {
                        if let image = renderedImage {
                            Image(uiImage: image)
                                .resizable()
                                .scaledToFit()
                                .frame(maxWidth: 320)
                                .padding(12)
                                .background(Color.white)
                                .clipShape(RoundedRectangle(cornerRadius: 14, style: .continuous))
                                .shadow(color: .black.opacity(0.08), radius: 12, y: 4)
                        } else if isRendering {
                            ProgressView("Rendering badge.svg…")
                                .frame(maxWidth: .infinity)
                                .padding(.vertical, 40)
                        } else {
                            Text("No image rendered")
                                .foregroundStyle(.secondary)
                                .frame(maxWidth: .infinity)
                                .padding(.vertical, 40)
                        }
                    }

                    VStack(alignment: .leading, spacing: 8) {
                        HStack {
                            Text("Scale")
                            Spacer()
                            Text(String(format: "%.2fx", scale))
                                .foregroundStyle(.secondary)
                        }
                        Slider(value: $scale, in: 0.5...2.0, step: 0.1)
                            .onChange(of: scale) { _ in
                                Task { await renderBadge() }
                            }
                    }

                    Button {
                        Task { await renderBadge() }
                    } label: {
                        Text("Re-render")
                            .frame(maxWidth: .infinity)
                    }
                    .buttonStyle(.borderedProminent)

                    if let errorMessage {
                        Text(errorMessage)
                            .font(.footnote)
                            .foregroundStyle(.red)
                            .frame(maxWidth: .infinity, alignment: .leading)
                    }

                    Text(sourceDescription)
                        .font(.footnote)
                        .foregroundStyle(.secondary)
                        .frame(maxWidth: .infinity, alignment: .leading)
                }
                .padding(20)
            }
            .background(Color(uiColor: .systemGroupedBackground))
            .navigationTitle("YepSVG Sample")
            .task {
                await renderBadge()
            }
        }
    }

    @MainActor
    private func renderBadge() async {
        isRendering = true
        errorMessage = nil
        defer { isRendering = false }

        let data: Data
        switch selectedSample {
        case .basic:
            data = BasicBadgeSVG.data
        case .complex:
            guard let complexData = BundledBadgeSVG.data else {
                errorMessage = "Failed to decode embedded complex SVG payload"
                renderedImage = nil
                return
            }
            data = complexData
        }

        var options = SVGRenderOptions.default
        options.scale = scale
        options.backgroundColor = UIColor.clear.cgColor

        do {
            renderedImage = try await renderer.render(svgData: data, options: options)
        } catch {
            renderedImage = nil
            errorMessage = "Render failed: \(error.localizedDescription)"
        }
    }

    private var sourceDescription: String {
        switch selectedSample {
        case .basic:
            return "Source: embedded basic.svg (simple supported shapes)"
        case .complex:
            return "Source: embedded badge.svg (from yepngo-assets/Badges/rewards/2025)"
        }
    }
}
