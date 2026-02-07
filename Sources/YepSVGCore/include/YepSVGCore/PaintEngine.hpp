#ifndef CHROMIUM_SVG_CORE_PAINT_ENGINE_HPP
#define CHROMIUM_SVG_CORE_PAINT_ENGINE_HPP

#include "YepSVGCore/CompatFlags.hpp"
#include "YepSVGCore/GeometryEngine.hpp"
#include "YepSVGCore/LayoutEngine.hpp"
#include "YepSVGCore/RasterBackendCG.hpp"
#include "YepSVGCore/StyleResolver.hpp"
#include "YepSVGCore/Types.hpp"

namespace csvg {

class PaintEngine {
public:
    bool Paint(const SvgDocument& document,
               const LayoutResult& layout,
               const RenderOptions& options,
               const CompatFlags& flags,
               RasterSurface& surface,
               RenderError& error) const;
};

} // namespace csvg

#endif
