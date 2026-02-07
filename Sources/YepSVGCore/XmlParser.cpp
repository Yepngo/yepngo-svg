#include "YepSVGCore/XmlParser.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
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

bool IsWhitespace(char c) {
    return std::isspace(static_cast<unsigned char>(c)) != 0;
}

std::string DecodeXmlEntities(const std::string& text) {
    std::string out;
    out.reserve(text.size());

    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '&') {
            out.push_back(text[i]);
            continue;
        }

        const size_t semi = text.find(';', i + 1);
        if (semi == std::string::npos) {
            out.push_back(text[i]);
            continue;
        }

        const std::string entity = text.substr(i + 1, semi - i - 1);
        auto append_code_point = [&out](uint32_t codepoint) {
            if (codepoint <= 0x7F) {
                out.push_back(static_cast<char>(codepoint));
            } else if (codepoint <= 0x7FF) {
                out.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
                out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
            } else if (codepoint <= 0xFFFF) {
                out.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
                out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
            } else {
                out.push_back(static_cast<char>(0xF0 | (codepoint >> 18)));
                out.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
            }
        };

        bool replaced = true;
        if (entity == "lt") {
            out.push_back('<');
        } else if (entity == "gt") {
            out.push_back('>');
        } else if (entity == "amp") {
            out.push_back('&');
        } else if (entity == "quot") {
            out.push_back('"');
        } else if (entity == "apos") {
            out.push_back('\'');
        } else if (!entity.empty() && entity[0] == '#') {
            uint32_t codepoint = 0;
            try {
                if (entity.size() > 2 && (entity[1] == 'x' || entity[1] == 'X')) {
                    codepoint = static_cast<uint32_t>(std::stoul(entity.substr(2), nullptr, 16));
                } else {
                    codepoint = static_cast<uint32_t>(std::stoul(entity.substr(1), nullptr, 10));
                }
            } catch (...) {
                replaced = false;
            }
            if (replaced) {
                append_code_point(codepoint);
            }
        } else {
            replaced = false;
        }

        if (replaced) {
            i = semi;
        } else {
            out.push_back(text[i]);
        }
    }

    return out;
}

std::map<std::string, std::string> ParseDoctypeEntities(const std::string& doctype_decl) {
    std::map<std::string, std::string> entities;

    const auto subset_begin = doctype_decl.find('[');
    const auto subset_end = doctype_decl.rfind(']');
    if (subset_begin == std::string::npos || subset_end == std::string::npos || subset_end <= subset_begin) {
        return entities;
    }

    const std::string subset = doctype_decl.substr(subset_begin + 1, subset_end - subset_begin - 1);
    size_t i = 0;
    while (i < subset.size()) {
        const auto entity_pos = subset.find("<!ENTITY", i);
        if (entity_pos == std::string::npos) {
            break;
        }

        size_t cursor = entity_pos + 8;
        while (cursor < subset.size() && IsWhitespace(subset[cursor])) {
            ++cursor;
        }

        size_t name_begin = cursor;
        while (cursor < subset.size() && !IsWhitespace(subset[cursor])) {
            ++cursor;
        }
        if (cursor <= name_begin) {
            i = entity_pos + 8;
            continue;
        }
        const std::string name = subset.substr(name_begin, cursor - name_begin);

        while (cursor < subset.size() && IsWhitespace(subset[cursor])) {
            ++cursor;
        }
        if (cursor >= subset.size() || (subset[cursor] != '"' && subset[cursor] != '\'')) {
            i = cursor;
            continue;
        }

        const char quote = subset[cursor];
        ++cursor;
        const size_t value_begin = cursor;
        while (cursor < subset.size() && subset[cursor] != quote) {
            ++cursor;
        }
        if (cursor >= subset.size()) {
            break;
        }

        entities[name] = subset.substr(value_begin, cursor - value_begin);

        const auto close = subset.find('>', cursor);
        if (close == std::string::npos) {
            break;
        }
        i = close + 1;
    }

    return entities;
}

std::string ExpandEntities(const std::string& text, const std::map<std::string, std::string>& entities) {
    if (entities.empty()) {
        return text;
    }

    std::string expanded = text;
    for (int pass = 0; pass < 8; ++pass) {
        bool changed = false;
        for (const auto& [name, value] : entities) {
            const std::string needle = "&" + name + ";";
            size_t pos = 0;
            while ((pos = expanded.find(needle, pos)) != std::string::npos) {
                expanded.replace(pos, needle.size(), value);
                pos += value.size();
                changed = true;
            }
        }
        if (!changed) {
            break;
        }
    }
    return expanded;
}

size_t FindDoctypeEnd(const std::string& text, size_t doctype_start) {
    bool in_single_quote = false;
    bool in_double_quote = false;
    int subset_depth = 0;
    for (size_t i = doctype_start; i < text.size(); ++i) {
        const char c = text[i];
        if (!in_double_quote && c == '\'') {
            in_single_quote = !in_single_quote;
            continue;
        }
        if (!in_single_quote && c == '"') {
            in_double_quote = !in_double_quote;
            continue;
        }
        if (in_single_quote || in_double_quote) {
            continue;
        }
        if (c == '[') {
            ++subset_depth;
            continue;
        }
        if (c == ']' && subset_depth > 0) {
            --subset_depth;
            continue;
        }
        if (c == '>' && subset_depth == 0) {
            return i;
        }
    }
    return std::string::npos;
}

std::string PreprocessDoctypeAndEntities(const std::string& text) {
    std::map<std::string, std::string> entities;
    std::string without_doctype;
    without_doctype.reserve(text.size());

    size_t cursor = 0;
    while (cursor < text.size()) {
        const auto doctype_pos = text.find("<!DOCTYPE", cursor);
        if (doctype_pos == std::string::npos) {
            without_doctype.append(text.substr(cursor));
            break;
        }

        without_doctype.append(text.substr(cursor, doctype_pos - cursor));
        const auto doctype_end = FindDoctypeEnd(text, doctype_pos);
        if (doctype_end == std::string::npos) {
            break;
        }

        const std::string doctype_decl = text.substr(doctype_pos, doctype_end - doctype_pos + 1);
        const auto parsed = ParseDoctypeEntities(doctype_decl);
        entities.insert(parsed.begin(), parsed.end());
        cursor = doctype_end + 1;
    }

    return ExpandEntities(without_doctype, entities);
}

} // namespace

std::optional<XmlNode> XmlParser::Parse(const std::string& text, RenderError& error) const {
    error = {};

    if (Trim(text).empty()) {
        error.code = RenderErrorCode::kInvalidDocument;
        error.message = "SVG input is empty";
        return std::nullopt;
    }

    const std::string preprocessed = PreprocessDoctypeAndEntities(text);

    static const std::regex kTokenRegex(R"((<[^>]+>)|([^<]+))");
    std::vector<XmlNode> node_stack;
    std::vector<size_t> child_indices;

    XmlNode root_holder;
    root_holder.name = "__root__";
    node_stack.push_back(root_holder);

    auto begin = std::sregex_iterator(preprocessed.begin(), preprocessed.end(), kTokenRegex);
    auto end = std::sregex_iterator();
    bool in_comment_block = false;

    for (auto it = begin; it != end; ++it) {
        const auto token = (*it).str();
        if (token.empty()) {
            continue;
        }

        if (in_comment_block) {
            if (token.find("-->") != std::string::npos) {
                in_comment_block = false;
            }
            continue;
        }

        if (token.rfind("<!--", 0) == 0) {
            if (token.find("-->") == std::string::npos) {
                in_comment_block = true;
            }
            continue;
        }

        if (token[0] != '<') {
            if (!node_stack.empty()) {
                if (token.find_first_not_of(" \t\r\n") == std::string::npos) {
                    continue;
                }
                node_stack.back().text += DecodeXmlEntities(token);
            }
            continue;
        }

        if (token.rfind("<?", 0) == 0 || token.rfind("<!", 0) == 0) {
            continue;
        }

        if (token.rfind("</", 0) == 0) {
            if (node_stack.size() <= 1) {
                error.code = RenderErrorCode::kInvalidDocument;
                error.message = "Malformed SVG: unexpected closing tag";
                return std::nullopt;
            }

            std::string close_tag = token.substr(2, token.size() - 3);
            close_tag = Trim(close_tag);
            if (!close_tag.empty() && node_stack.back().name != close_tag) {
                error.code = RenderErrorCode::kInvalidDocument;
                error.message = "Malformed SVG: closing tag mismatch";
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
        for (auto& [key, value] : node.attributes) {
            value = DecodeXmlEntities(value);
        }

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
