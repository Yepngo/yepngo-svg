import Foundation
import Kingfisher
import UIKit
import YepSVG

public struct YepSVGProcessor: ImageProcessor {
    public let identifier: String

    private let renderOptions: SVGRenderOptions

    public init(options: SVGRenderOptions = .default) {
        self.renderOptions = options
        self.identifier = "com.yep.svg.kingfisher.processor(scale:\(options.scale),font:\(options.defaultFontFamily),fontSize:\(options.defaultFontSize),external:\(options.enableExternalResources))"
    }

    public func process(item: ImageProcessItem, options: KingfisherParsedOptionsInfo) -> KFCrossPlatformImage? {
        switch item {
        case .image(let image):
            return image
        case .data(let data):
            return try? SVGRenderer.renderSync(svgData: data, options: renderOptions)
        }
    }
}
