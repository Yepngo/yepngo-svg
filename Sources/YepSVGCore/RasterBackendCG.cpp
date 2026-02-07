#include "YepSVGCore/RasterBackendCG.hpp"

#include <cstring>

namespace csvg {

RasterSurface::RasterSurface(int32_t width, int32_t height, const Color& background)
    : width_(width), height_(height), bytes_(static_cast<size_t>(width) * static_cast<size_t>(height) * 4u, 0), context_(nullptr) {
    CGColorSpaceRef color_space = CGColorSpaceCreateDeviceRGB();
    context_ = CGBitmapContextCreate(bytes_.data(),
                                     static_cast<size_t>(width),
                                     static_cast<size_t>(height),
                                     8,
                                     static_cast<size_t>(width) * 4,
                                     color_space,
                                     kCGImageAlphaPremultipliedLast | kCGBitmapByteOrderDefault);
    CGColorSpaceRelease(color_space);

    if (context_ != nullptr && background.is_valid && !background.is_none) {
        CGContextSetRGBFillColor(context_, background.r, background.g, background.b, background.a);
        CGContextFillRect(context_, CGRectMake(0, 0, static_cast<CGFloat>(width), static_cast<CGFloat>(height)));
    }
}

RasterSurface::~RasterSurface() {
    if (context_ != nullptr) {
        CGContextRelease(context_);
    }
}

CGContextRef RasterSurface::context() const {
    return context_;
}

bool RasterSurface::Extract(ImageBuffer& out, RenderError& error) const {
    if (context_ == nullptr) {
        error.code = RenderErrorCode::kRenderFailed;
        error.message = "Failed to create bitmap context";
        return false;
    }

    out.width = width_;
    out.height = height_;
    out.rgba = bytes_;
    return true;
}

} // namespace csvg
