#include "YepSVGCore/XmlParser.hpp"

#include <algorithm>
#include <regex>
#include <sstream>
#include <stack>

namespace csvg {
namespace {

std::string Trim(const std::string& value) {
    const auto begin = value.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) {
        return "";
    }
    const auto end = value.find_last_not_of(" \t\r\n");
    return value.substr(begin, end - begin + 1);
}

std::map<std::string, std::string> ParseAttributes(const std::string& raw) {
    static const std::regex kAttrRegex(R"CSVG(([A-Za-z_:][-A-Za-z0-9_:.]*)\s*=\s*("([^"]*)"|'([^']*)'))CSVG");
    std::map<std::string, std::string> out;
    auto begin = std::sregex_iterator(raw.begin(), raw.end(), kAttrRegex);
    auto end = std::sregex_iterator();
    for (auto it = begin; it != end; ++it) {
        const auto key = (*it)[1].str();
        const auto value = (*it)[3].matched ? (*it)[3].str() : (*it)[4].str();
        out[key] = value;
    }
    return out;
}

} // namespace

std::optional<XmlNode> XmlParser::Parse(const std::string& text, RenderError& error) const {
    error = {};

    if (Trim(text).empty()) {
        error.code = RenderErrorCode::kInvalidDocument;
        error.message = "SVG input is empty";
        return std::nullopt;
    }

    static const std::regex kTokenRegex(R"((<[^>]+>)|([^<]+))");
    std::vector<XmlNode> node_stack;
    std::vector<size_t> child_indices;

    XmlNode root_holder;
    root_holder.name = "__root__";
    node_stack.push_back(root_holder);

    auto begin = std::sregex_iterator(text.begin(), text.end(), kTokenRegex);
    auto end = std::sregex_iterator();

    for (auto it = begin; it != end; ++it) {
        const auto token = (*it).str();
        if (token.empty()) {
            continue;
        }

        if (token[0] != '<') {
            const auto content = Trim(token);
            if (!content.empty() && !node_stack.empty()) {
                node_stack.back().text += content;
            }
            continue;
        }

        if (token.rfind("<!--", 0) == 0 || token.rfind("<?", 0) == 0 || token.rfind("<!DOCTYPE", 0) == 0) {
            continue;
        }

        if (token.rfind("</", 0) == 0) {
            if (node_stack.size() <= 1) {
                error.code = RenderErrorCode::kInvalidDocument;
                error.message = "Malformed SVG: unexpected closing tag";
                return std::nullopt;
            }

            XmlNode closed = node_stack.back();
            node_stack.pop_back();
            node_stack.back().children.push_back(std::move(closed));
            continue;
        }

        const bool self_closing = token.size() > 2 && token[token.size() - 2] == '/';
        std::string inner = token.substr(1, token.size() - 2);
        if (self_closing && !inner.empty()) {
            inner.pop_back();
        }
        inner = Trim(inner);
        if (inner.empty()) {
            continue;
        }

        std::string tag_name;
        std::string attr_blob;

        const auto split = inner.find_first_of(" \t\r\n");
        if (split == std::string::npos) {
            tag_name = inner;
        } else {
            tag_name = inner.substr(0, split);
            attr_blob = inner.substr(split + 1);
        }

        XmlNode node;
        node.name = tag_name;
        node.attributes = ParseAttributes(attr_blob);

        if (self_closing) {
            node_stack.back().children.push_back(std::move(node));
        } else {
            node_stack.push_back(std::move(node));
        }
    }

    while (node_stack.size() > 1) {
        XmlNode closed = node_stack.back();
        node_stack.pop_back();
        node_stack.back().children.push_back(std::move(closed));
    }

    if (node_stack.empty() || node_stack.front().children.empty()) {
        error.code = RenderErrorCode::kInvalidDocument;
        error.message = "Malformed SVG: no root element";
        return std::nullopt;
    }

    return node_stack.front().children.front();
}

} // namespace csvg
