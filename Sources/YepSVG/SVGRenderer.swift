import Foundation
import UIKit

public final class SVGRenderer: @unchecked Sendable {
    private let loader: (any SVGExternalResourceLoader)?

    public init(loader: (any SVGExternalResourceLoader)? = nil) {
        self.loader = loader
    }

    public func render(svgString: String, options: SVGRenderOptions) async throws -> UIImage {
        guard let data = svgString.data(using: .utf8) else {
            throw SVGRenderError.invalidDocument("Input string is not valid UTF-8")
        }
        return try await render(svgData: data, options: options)
    }

    public func render(svgData: Data, options: SVGRenderOptions) async throws -> UIImage {
        guard !svgData.isEmpty else {
            throw SVGRenderError.invalidDocument("Input data is empty")
        }

        try await preflightExternalResourcesIfNeeded(svgData: svgData, options: options)

        return try await Task.detached(priority: .userInitiated) {
            try Self.renderSync(svgData: svgData, options: options)
        }.value
    }

    public func render(svgFileURL: URL, options: SVGRenderOptions) async throws -> UIImage {
        let data: Data
        do {
            data = try Data(contentsOf: svgFileURL)
        } catch {
            throw SVGRenderError.invalidDocument("Failed to load SVG at \(svgFileURL.path): \(error.localizedDescription)")
        }

        let rewritten = rewriteRelativeResourceReferences(in: data, relativeTo: svgFileURL.deletingLastPathComponent())
        return try await render(svgData: rewritten, options: options)
    }

    public static func renderSync(svgData: Data, options: SVGRenderOptions) throws -> UIImage {
        guard !svgData.isEmpty else {
            throw SVGRenderError.invalidDocument("Input data is empty")
        }
        return try SVGCoreBridge.render(svgData: svgData, options: options)
    }

    private func preflightExternalResourcesIfNeeded(svgData: Data, options: SVGRenderOptions) async throws {
        let urls = extractExternalURLs(from: svgData)
        guard !urls.isEmpty else {
            return
        }

        if !options.enableExternalResources {
            if let blocked = urls.first(where: { $0.scheme?.hasPrefix("http") == true }) {
                throw SVGRenderError.externalResourceBlocked(blocked)
            }
            return
        }

        guard let loader else {
            if let blocked = urls.first(where: { $0.scheme?.hasPrefix("http") == true }) {
                throw SVGRenderError.externalResourceBlocked(blocked)
            }
            return
        }

        for url in urls {
            let request = SVGExternalResourceRequest(url: url, purpose: inferPurpose(for: url))
            do {
                _ = try await loader.loadResource(request)
            } catch {
                throw SVGRenderError.externalResourceFailed(url, error.localizedDescription)
            }
        }
    }

    private func extractExternalURLs(from svgData: Data) -> [URL] {
        guard let text = String(data: svgData, encoding: .utf8) else {
            return []
        }

        let pattern = #"(?:href|xlink:href)\s*=\s*"([^"]+)""#
        guard let regex = try? NSRegularExpression(pattern: pattern) else {
            return []
        }

        let range = NSRange(text.startIndex..<text.endIndex, in: text)
        return regex.matches(in: text, options: [], range: range).compactMap { match in
            guard let hrefRange = Range(match.range(at: 1), in: text) else {
                return nil
            }
            let value = String(text[hrefRange])
            if value.hasPrefix("#") {
                return nil
            }
            return URL(string: value)
        }
    }

    private func inferPurpose(for url: URL) -> SVGExternalResourceRequest.Purpose {
        let ext = url.pathExtension.lowercased()
        if ["png", "jpg", "jpeg", "gif", "webp", "svg"].contains(ext) {
            return .image
        }
        if ext == "css" {
            return .stylesheet
        }
        if ["ttf", "otf", "woff", "woff2"].contains(ext) {
            return .font
        }
        return .other
    }

    private func rewriteRelativeResourceReferences(in svgData: Data, relativeTo baseDirectoryURL: URL) -> Data {
        guard let text = String(data: svgData, encoding: .utf8) else {
            return svgData
        }

        let pattern = #"(?:href|xlink:href)\s*=\s*(['"])([^'"]+)\1"#
        guard let regex = try? NSRegularExpression(pattern: pattern) else {
            return svgData
        }

        let mutable = NSMutableString(string: text)
        let range = NSRange(location: 0, length: mutable.length)
        let matches = regex.matches(in: text, options: [], range: range)
        for match in matches.reversed() {
            guard match.numberOfRanges >= 3 else { continue }
            let hrefRange = match.range(at: 2)
            guard hrefRange.location != NSNotFound, hrefRange.length > 0 else { continue }

            let href = mutable.substring(with: hrefRange).trimmingCharacters(in: .whitespacesAndNewlines)
            guard shouldResolveRelativePath(href) else { continue }

            let resolved = URL(fileURLWithPath: href, relativeTo: baseDirectoryURL).standardizedFileURL.absoluteString
            mutable.replaceCharacters(in: hrefRange, with: resolved)
        }

        return mutable.data(using: String.Encoding.utf8.rawValue) ?? svgData
    }

    private func shouldResolveRelativePath(_ href: String) -> Bool {
        if href.isEmpty || href.hasPrefix("#") || href.hasPrefix("data:") || href.hasPrefix("/") {
            return false
        }
        if let scheme = URL(string: href)?.scheme, !scheme.isEmpty {
            return false
        }
        return true
    }
}
