#ifndef CHROMIUM_SVG_CORE_SVG_DOM_HPP
#define CHROMIUM_SVG_CORE_SVG_DOM_HPP

#include <optional>

#include "YepSVGCore/Types.hpp"

namespace csvg {

class SvgDom {
public:
    std::optional<SvgDocument> Build(const XmlNode& root, RenderError& error) const;
};

} // namespace csvg

#endif
