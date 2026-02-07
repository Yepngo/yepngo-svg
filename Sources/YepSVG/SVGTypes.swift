import Foundation
import CoreGraphics

public struct SVGRenderOptions: @unchecked Sendable {
    public var viewportSize: CGSize?
    public var scale: CGFloat
    public var backgroundColor: CGColor?
    public var defaultFontFamily: String
    public var defaultFontSize: CGFloat
    public var enableExternalResources: Bool

    public init(
        viewportSize: CGSize? = nil,
        scale: CGFloat = 1.0,
        backgroundColor: CGColor? = nil,
        defaultFontFamily: String = "Helvetica",
        defaultFontSize: CGFloat = 16,
        enableExternalResources: Bool = false
    ) {
        self.viewportSize = viewportSize
        self.scale = scale
        self.backgroundColor = backgroundColor
        self.defaultFontFamily = defaultFontFamily
        self.defaultFontSize = defaultFontSize
        self.enableExternalResources = enableExternalResources
    }

    public static let `default` = SVGRenderOptions()
}

public struct SVGExternalResourceRequest: Sendable {
    public let url: URL
    public let purpose: Purpose

    public enum Purpose: Sendable {
        case image
        case stylesheet
        case font
        case other
    }

    public init(url: URL, purpose: Purpose) {
        self.url = url
        self.purpose = purpose
    }
}

public protocol SVGExternalResourceLoader: Sendable {
    func loadResource(_ request: SVGExternalResourceRequest) async throws -> Data
}

public enum SVGRenderError: Error, Sendable, LocalizedError {
    case invalidDocument(String)
    case unsupportedFeature(String)
    case externalResourceBlocked(URL)
    case externalResourceFailed(URL, String)
    case renderFailed(String)

    public var errorDescription: String? {
        switch self {
        case .invalidDocument(let message):
            return "Invalid SVG document: \(message)"
        case .unsupportedFeature(let message):
            return "Unsupported SVG feature: \(message)"
        case .externalResourceBlocked(let url):
            return "External resource blocked: \(url.absoluteString)"
        case .externalResourceFailed(let url, let message):
            return "External resource failed (\(url.absoluteString)): \(message)"
        case .renderFailed(let message):
            return "SVG render failed: \(message)"
        }
    }
}
