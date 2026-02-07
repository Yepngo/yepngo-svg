#include "YepSVGCore/FilterGraph.hpp"

#include <set>

namespace csvg {
namespace {

bool TraverseFilterNodes(const XmlNode& node, const std::set<std::string>& supported, RenderError& error) {
    if (node.name == "filter") {
        for (const auto& child : node.children) {
            if (supported.find(child.name) == supported.end()) {
                error.code = RenderErrorCode::kUnsupportedFeature;
                error.message = "Unsupported filter primitive: " + child.name;
                return false;
            }
        }
    }

    for (const auto& child : node.children) {
        if (!TraverseFilterNodes(child, supported, error)) {
            return false;
        }
    }
    return true;
}

} // namespace

bool FilterGraph::ValidateFilterSupport(const XmlNode& node, const CompatFlags& flags, RenderError& error) const {
    static const std::set<std::string> kSupported = {
        "feGaussianBlur",
        "feOffset",
        "feBlend",
        "feColorMatrix",
        "feComposite",
        "feFlood",
        "feMerge",
        "feMergeNode",
    };

    error = {};
    if (TraverseFilterNodes(node, kSupported, error)) {
        return true;
    }

    if (!flags.strict_mode || flags.allow_unsupported_filter_fallback) {
        error = {};
        return true;
    }
    return false;
}

} // namespace csvg
