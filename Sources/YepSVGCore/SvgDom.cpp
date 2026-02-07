#include "YepSVGCore/SvgDom.hpp"

namespace csvg {

std::optional<SvgDocument> SvgDom::Build(const XmlNode& root, RenderError& error) const {
    error = {};

    if (root.name != "svg") {
        error.code = RenderErrorCode::kInvalidDocument;
        error.message = "Root element must be <svg>";
        return std::nullopt;
    }

    SvgDocument document;
    document.root = root;
    return document;
}

} // namespace csvg
