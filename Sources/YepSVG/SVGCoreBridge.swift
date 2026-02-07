import CoreGraphics
import Foundation
import UIKit
import YepSVGCBridge

enum SVGCoreBridge {
    static func render(svgData: Data, options: SVGRenderOptions) throws -> UIImage {
        guard let renderer = csvg_renderer_create() else {
            throw SVGRenderError.renderFailed("Failed to initialize core renderer")
        }
        defer { csvg_renderer_destroy(renderer) }

        var cOptions = csvg_render_options_t()
        csvg_render_options_init_default(&cOptions)

        if let viewport = options.viewportSize {
            cOptions.viewport_width = Int32(viewport.width.rounded(.toNearestOrAwayFromZero))
            cOptions.viewport_height = Int32(viewport.height.rounded(.toNearestOrAwayFromZero))
        }
        cOptions.scale = Float(options.scale)
        cOptions.default_font_size = Float(options.defaultFontSize)
        cOptions.enable_external_resources = options.enableExternalResources

        if let color = options.backgroundColor,
           let components = color.components {
            if color.numberOfComponents >= 4 {
                cOptions.background_red = Float(components[0])
                cOptions.background_green = Float(components[1])
                cOptions.background_blue = Float(components[2])
                cOptions.background_alpha = Float(components[3])
            } else if color.numberOfComponents == 2 {
                cOptions.background_red = Float(components[0])
                cOptions.background_green = Float(components[0])
                cOptions.background_blue = Float(components[0])
                cOptions.background_alpha = Float(components[1])
            }
        }

        var result = csvg_render_result_t()
        let fontFamily = options.defaultFontFamily
        let status: Int32 = fontFamily.withCString { fontCString in
            cOptions.default_font_family = fontCString
            return svgData.withUnsafeBytes { rawBuffer in
                guard let baseAddress = rawBuffer.baseAddress?.assumingMemoryBound(to: UInt8.self) else {
                    return 0
                }
                return csvg_renderer_render(renderer, baseAddress, rawBuffer.count, &cOptions, &result)
            }
        }
        defer { csvg_render_result_free(&result) }

        guard status == 1 else {
            let message = result.error_message.map { String(cString: $0) } ?? "Unknown C bridge render failure"
            throw mapError(code: result.error_code, message: message)
        }

        guard result.width > 0, result.height > 0, let rgba = result.rgba, result.rgba_size > 0 else {
            throw SVGRenderError.renderFailed("Core renderer returned an empty image")
        }

        let byteCount = Int(result.rgba_size)
        let data = Data(bytes: rgba, count: byteCount)

        guard let provider = CGDataProvider(data: data as CFData) else {
            throw SVGRenderError.renderFailed("Failed to create CGDataProvider")
        }

        let colorSpace = CGColorSpaceCreateDeviceRGB()
        guard let cgImage = CGImage(
            width: Int(result.width),
            height: Int(result.height),
            bitsPerComponent: 8,
            bitsPerPixel: 32,
            bytesPerRow: Int(result.width) * 4,
            space: colorSpace,
            bitmapInfo: CGBitmapInfo(rawValue: CGImageAlphaInfo.premultipliedLast.rawValue),
            provider: provider,
            decode: nil,
            shouldInterpolate: true,
            intent: .defaultIntent
        ) else {
            throw SVGRenderError.renderFailed("Failed to create CGImage from pixel buffer")
        }

        return UIImage(cgImage: cgImage)
    }

    private static func mapError(code: csvg_error_code_t, message: String) -> SVGRenderError {
        switch code {
        case CSVG_ERROR_INVALID_DOCUMENT:
            return .invalidDocument(message)
        case CSVG_ERROR_UNSUPPORTED_FEATURE:
            return .unsupportedFeature(message)
        case CSVG_ERROR_EXTERNAL_RESOURCE_BLOCKED:
            if let url = URL(string: message.replacingOccurrences(of: "External resource blocked: ", with: "")) {
                return .externalResourceBlocked(url)
            }
            return .externalResourceBlocked(URL(string: "about:invalid")!)
        case CSVG_ERROR_EXTERNAL_RESOURCE_FAILED:
            return .renderFailed(message)
        case CSVG_ERROR_RENDER_FAILED:
            return .renderFailed(message)
        case CSVG_ERROR_NONE:
            return .renderFailed(message)
        default:
            return .renderFailed(message)
        }
    }
}
