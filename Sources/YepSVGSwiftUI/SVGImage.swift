import SwiftUI
import UIKit
import YepSVG

public struct SVGImage: View {
    private let svgData: Data
    private let renderer: SVGRenderer
    private let options: SVGRenderOptions

    @State private var phase: Phase = .loading

    private enum Phase {
        case loading
        case success(UIImage)
        case failure
    }

    public init(svgData: Data, renderer: SVGRenderer, options: SVGRenderOptions = .default) {
        self.svgData = svgData
        self.renderer = renderer
        self.options = options
    }

    public var body: some View {
        Group {
            switch phase {
            case .loading:
                ProgressView()
            case .success(let image):
                Image(uiImage: image)
                    .resizable()
                    .scaledToFit()
            case .failure:
                Image(systemName: "exclamationmark.triangle")
                    .foregroundColor(.red)
            }
        }
        .task(id: svgData) {
            await render()
        }
    }

    @MainActor
    private func render() async {
        phase = .loading
        do {
            let image = try await renderer.render(svgData: svgData, options: options)
            phase = .success(image)
        } catch {
            phase = .failure
        }
    }
}
