#ifndef CHROMIUM_SVG_CORE_XML_PARSER_HPP
#define CHROMIUM_SVG_CORE_XML_PARSER_HPP

#include <optional>
#include <string>

#include "YepSVGCore/Types.hpp"

namespace csvg {

class XmlParser {
public:
    std::optional<XmlNode> Parse(const std::string& text, RenderError& error) const;
};

} // namespace csvg

#endif
