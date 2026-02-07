#ifndef CHROMIUM_SVG_CORE_RASTER_BACKEND_CG_HPP
#define CHROMIUM_SVG_CORE_RASTER_BACKEND_CG_HPP

#include <CoreGraphics/CoreGraphics.h>

#include <vector>

#include "YepSVGCore/Types.hpp"

namespace csvg {

class RasterSurface {
public:
    RasterSurface(int32_t width, int32_t height, const Color& background);
    ~RasterSurface();

    RasterSurface(const RasterSurface&) = delete;
    RasterSurface& operator=(const RasterSurface&) = delete;

    CGContextRef context() const;
    bool Extract(ImageBuffer& out, RenderError& error) const;

private:
    int32_t width_;
    int32_t height_;
    std::vector<uint8_t> bytes_;
    CGContextRef context_;
};

} // namespace csvg

#endif
