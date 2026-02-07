#ifndef CHROMIUM_SVG_CORE_LAYOUT_ENGINE_HPP
#define CHROMIUM_SVG_CORE_LAYOUT_ENGINE_HPP

#include <optional>

#include "YepSVGCore/Types.hpp"

namespace csvg {

struct LayoutResult {
    int32_t width = 300;
    int32_t height = 150;
    double view_box_x = 0.0;
    double view_box_y = 0.0;
    double view_box_width = 0.0;
    double view_box_height = 0.0;
};

class LayoutEngine {
public:
    std::optional<LayoutResult> Compute(const SvgDocument& document, const RenderOptions& options, RenderError& error) const;
};

} // namespace csvg

#endif
