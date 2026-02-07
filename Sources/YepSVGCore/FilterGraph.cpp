#include "YepSVGCore/FilterGraph.hpp"

#include <algorithm>
#include <cctype>
#include <set>

namespace csvg {
namespace {

std::string Lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

std::string LocalName(const std::string& name) {
    const auto separator = name.rfind(':');
    if (separator == std::string::npos) {
        return Lower(name);
    }
    return Lower(name.substr(separator + 1));
}

bool TraverseFilterNodes(const XmlNode& node, const std::set<std::string>& supported, RenderError& error) {
    if (LocalName(node.name) == "filter") {
        for (const auto& child : node.children) {
            const auto primitive_name = LocalName(child.name);
            if (supported.find(primitive_name) == supported.end()) {
                error.code = RenderErrorCode::kUnsupportedFeature;
                error.message = "Unsupported filter primitive: " + primitive_name;
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
        "fegaussianblur",
        "feoffset",
        "feblend",
        "fecolormatrix",
        "fecomposite",
        "fecomponenttransfer",
        "feconvolvematrix",
        "fediffuselighting",
        "fedisplacementmap",
        "feflood",
        "femerge",
        "femergenode",
        "feimage",
        "femorphology",
        "fespecularlighting",
        "fetile",
        "feturbulence",
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
