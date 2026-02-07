#ifndef CHROMIUM_SVG_CORE_RESOURCE_RESOLVER_HPP
#define CHROMIUM_SVG_CORE_RESOURCE_RESOLVER_HPP

#include <string>
#include <vector>

#include "YepSVGCore/Types.hpp"

namespace csvg {

class ResourceResolver {
public:
    std::vector<std::string> CollectExternalURLs(const XmlNode& node) const;
    bool ValidatePolicy(const std::vector<std::string>& urls, const RenderOptions& options, RenderError& error) const;
};

} // namespace csvg

#endif
