#ifndef CHROMIUM_SVG_CORE_FILTER_GRAPH_HPP
#define CHROMIUM_SVG_CORE_FILTER_GRAPH_HPP

#include "YepSVGCore/CompatFlags.hpp"
#include "YepSVGCore/Types.hpp"

namespace csvg {

class FilterGraph {
public:
    bool ValidateFilterSupport(const XmlNode& node, const CompatFlags& flags, RenderError& error) const;
};

} // namespace csvg

#endif
