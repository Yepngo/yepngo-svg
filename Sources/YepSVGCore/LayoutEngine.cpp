#include "YepSVGCore/LayoutEngine.hpp"

#include <cstdlib>
#include <sstream>
#include <string>

namespace csvg {
namespace {

double ParseDimension(const std::map<std::string, std::string>& attrs, const std::string& key, double fallback) {
    const auto it = attrs.find(key);
    if (it == attrs.end()) {
        return fallback;
    }

    const std::string raw = it->second;
    if (raw.empty()) {
        return fallback;
    }

    std::string cleaned;
    cleaned.reserve(raw.size());
    for (char c : raw) {
        if ((c >= '0' && c <= '9') || c == '.' || c == '-' || c == '+') {
            cleaned.push_back(c);
        }
    }

    char* end_ptr = nullptr;
    const double parsed = std::strtod(cleaned.c_str(), &end_ptr);
    if (end_ptr == cleaned.c_str()) {
        return fallback;
    }

    if (raw.find('%') != std::string::npos) {
        return fallback * parsed / 100.0;
    }
    return parsed;
}

} // namespace

std::optional<LayoutResult> LayoutEngine::Compute(const SvgDocument& document, const RenderOptions& options, RenderError& error) const {
    error = {};

    LayoutResult layout;
    const auto& attrs = document.root.attributes;

    const auto viewbox_it = attrs.find("viewBox");
    if (viewbox_it != attrs.end()) {
        std::stringstream stream(viewbox_it->second);
        stream >> layout.view_box_x >> layout.view_box_y >> layout.view_box_width >> layout.view_box_height;
    }

    const bool has_viewbox = layout.view_box_width > 0.0 && layout.view_box_height > 0.0;

    const double fallback_width = options.viewport_width > 0 ? static_cast<double>(options.viewport_width)
                                                             : (has_viewbox ? layout.view_box_width : 300.0);
    const double fallback_height = options.viewport_height > 0 ? static_cast<double>(options.viewport_height)
                                                               : (has_viewbox ? layout.view_box_height : 150.0);

    layout.width = static_cast<int32_t>(ParseDimension(attrs, "width", fallback_width));
    layout.height = static_cast<int32_t>(ParseDimension(attrs, "height", fallback_height));

    if (layout.width <= 0 || layout.height <= 0) {
        error.code = RenderErrorCode::kInvalidDocument;
        error.message = "Invalid SVG viewport dimensions";
        return std::nullopt;
    }

    if (!has_viewbox) {
        layout.view_box_x = 0.0;
        layout.view_box_y = 0.0;
        layout.view_box_width = static_cast<double>(layout.width);
        layout.view_box_height = static_cast<double>(layout.height);
    }

    layout.width = static_cast<int32_t>(static_cast<double>(layout.width) * options.scale);
    layout.height = static_cast<int32_t>(static_cast<double>(layout.height) * options.scale);

    return layout;
}

} // namespace csvg
