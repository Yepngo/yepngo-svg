#ifndef CHROMIUM_SVG_CORE_STYLE_RESOLVER_HPP
#define CHROMIUM_SVG_CORE_STYLE_RESOLVER_HPP

#include <map>
#include <vector>

#include <optional>
#include <string>

#include "YepSVGCore/Types.hpp"

namespace csvg {

struct ResolvedStyle {
    Color color;
    Color fill;
    Color stroke;
    std::string fill_paint;
    std::string stroke_paint;
    std::string color_paint;

    float fill_opacity = 1.0f;
    float stroke_opacity = 1.0f;
    float stroke_width = 1.0f;
    float opacity = 1.0f;

    std::string fill_rule = "nonzero";
    std::string stroke_line_cap = "butt";
    std::string stroke_line_join = "miter";
    float stroke_miter_limit = 4.0f;
    std::vector<float> stroke_dasharray;
    float stroke_dashoffset = 0.0f;

    std::string font_family;
    float font_size = 0.0f;
    int font_weight = 400;
    std::string font_style = "normal";
    std::string text_decoration = "none";
    float letter_spacing = 0.0f;
    float word_spacing = 0.0f;
    std::string text_anchor = "start";
};

class StyleResolver {
public:
    ResolvedStyle Resolve(const XmlNode& node,
                          const ResolvedStyle* parent,
                          const RenderOptions& options,
                          const std::map<std::string, std::string>* matched_css_properties = nullptr) const;
    static Color ParseColor(const std::string& value);
};

} // namespace csvg

#endif
