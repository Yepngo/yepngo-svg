#include "YepSVGCore/StyleResolver.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace csvg {
namespace {

std::string Lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

std::string Trim(const std::string& value) {
    const auto begin = value.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) {
        return "";
    }
    const auto end = value.find_last_not_of(" \t\r\n");
    return value.substr(begin, end - begin + 1);
}

float ParseFloat(const std::string& value, float fallback) {
    const auto trimmed = Trim(value);
    if (trimmed.empty()) {
        return fallback;
    }

    char* end_ptr = nullptr;
    const float parsed = std::strtof(trimmed.c_str(), &end_ptr);
    if (end_ptr == trimmed.c_str()) {
        return fallback;
    }

    if (*end_ptr == '%') {
        return parsed / 100.0f;
    }
    return parsed;
}

std::map<std::string, std::string> ParseInlineStyle(const std::string& style_text) {
    std::map<std::string, std::string> out;
    std::stringstream stream(style_text);
    std::string token;
    while (std::getline(stream, token, ';')) {
        const auto separator = token.find(':');
        if (separator == std::string::npos) {
            continue;
        }
        const auto key = Lower(Trim(token.substr(0, separator)));
        const auto value = Trim(token.substr(separator + 1));
        if (!key.empty() && !value.empty()) {
            out[key] = value;
        }
    }
    return out;
}

std::optional<float> ParseCssFloat(const std::string& value) {
    const auto trimmed = Trim(value);
    if (trimmed.empty()) {
        return std::nullopt;
    }

    char* end_ptr = nullptr;
    const float parsed = std::strtof(trimmed.c_str(), &end_ptr);
    if (end_ptr == trimmed.c_str()) {
        return std::nullopt;
    }

    return parsed;
}

float ParseCssByteComponent(const std::string& value) {
    const auto trimmed = Trim(value);
    if (trimmed.empty()) {
        return 0.0f;
    }

    if (trimmed.back() == '%') {
        const auto ratio = ParseCssFloat(trimmed.substr(0, trimmed.size() - 1)).value_or(0.0f) / 100.0f;
        return std::clamp(ratio, 0.0f, 1.0f);
    }

    const auto raw = ParseCssFloat(trimmed).value_or(0.0f);
    return std::clamp(raw / 255.0f, 0.0f, 1.0f);
}

float ParseCssAlphaComponent(const std::string& value) {
    const auto trimmed = Trim(value);
    if (trimmed.empty()) {
        return 1.0f;
    }

    if (trimmed.back() == '%') {
        const auto ratio = ParseCssFloat(trimmed.substr(0, trimmed.size() - 1)).value_or(100.0f) / 100.0f;
        return std::clamp(ratio, 0.0f, 1.0f);
    }

    return std::clamp(ParseCssFloat(trimmed).value_or(1.0f), 0.0f, 1.0f);
}

std::vector<float> ParseFloatList(const std::string& value) {
    std::vector<float> out;
    const auto trimmed = Lower(Trim(value));
    if (trimmed.empty() || trimmed == "none") {
        return out;
    }

    size_t i = 0;
    while (i < trimmed.size()) {
        while (i < trimmed.size() && (std::isspace(static_cast<unsigned char>(trimmed[i])) || trimmed[i] == ',')) {
            ++i;
        }
        if (i >= trimmed.size()) {
            break;
        }

        const char* start = trimmed.c_str() + i;
        char* end_ptr = nullptr;
        const float parsed = std::strtof(start, &end_ptr);
        if (end_ptr == start) {
            ++i;
            continue;
        }
        out.push_back(std::max(parsed, 0.0f));
        i = static_cast<size_t>(end_ptr - trimmed.c_str());
    }

    if (out.size() % 2 == 1) {
        const auto duplicated = out;
        out.insert(out.end(), duplicated.begin(), duplicated.end());
    }
    return out;
}

std::vector<std::string> SplitCssFunctionArgs(const std::string& args) {
    std::vector<std::string> tokens;
    std::string current;
    for (char c : args) {
        if (c == ',' || std::isspace(static_cast<unsigned char>(c)) || c == '/') {
            if (!current.empty()) {
                tokens.push_back(current);
                current.clear();
            }
            continue;
        }
        current.push_back(c);
    }
    if (!current.empty()) {
        tokens.push_back(current);
    }
    return tokens;
}

Color ParseFunctionColor(const std::string& lower) {
    const auto open = lower.find('(');
    const auto close = lower.rfind(')');
    if (open == std::string::npos || close == std::string::npos || close <= open + 1) {
        return {};
    }

    const auto name = lower.substr(0, open);
    const auto args = SplitCssFunctionArgs(lower.substr(open + 1, close - open - 1));

    if (name == "rgb" || name == "rgba") {
        if (args.size() < 3) {
            return {};
        }
        Color color;
        color.is_valid = true;
        color.is_none = false;
        color.r = ParseCssByteComponent(args[0]);
        color.g = ParseCssByteComponent(args[1]);
        color.b = ParseCssByteComponent(args[2]);
        color.a = args.size() >= 4 ? ParseCssAlphaComponent(args[3]) : 1.0f;
        return color;
    }

    return {};
}

Color NamedColor(float r, float g, float b, float a = 1.0f) {
    Color color;
    color.is_none = false;
    color.is_valid = true;
    color.r = r;
    color.g = g;
    color.b = b;
    color.a = a;
    return color;
}

} // namespace

Color StyleResolver::ParseColor(const std::string& value) {
    const auto lower = Lower(Trim(value));
    if (lower.empty()) {
        return {};
    }

    if (lower == "none") {
        Color color;
        color.is_none = true;
        color.is_valid = true;
        return color;
    }

    if (!lower.empty() && lower[0] == '#') {
        if (lower.size() == 7) {
            Color color;
            color.is_valid = true;
            color.r = static_cast<float>(std::stoi(lower.substr(1, 2), nullptr, 16)) / 255.0f;
            color.g = static_cast<float>(std::stoi(lower.substr(3, 2), nullptr, 16)) / 255.0f;
            color.b = static_cast<float>(std::stoi(lower.substr(5, 2), nullptr, 16)) / 255.0f;
            color.a = 1.0f;
            return color;
        }
        if (lower.size() == 9) {
            Color color;
            color.is_valid = true;
            color.r = static_cast<float>(std::stoi(lower.substr(1, 2), nullptr, 16)) / 255.0f;
            color.g = static_cast<float>(std::stoi(lower.substr(3, 2), nullptr, 16)) / 255.0f;
            color.b = static_cast<float>(std::stoi(lower.substr(5, 2), nullptr, 16)) / 255.0f;
            color.a = static_cast<float>(std::stoi(lower.substr(7, 2), nullptr, 16)) / 255.0f;
            return color;
        }
        if (lower.size() == 4) {
            Color color;
            color.is_valid = true;
            color.r = static_cast<float>(std::stoi(std::string(2, lower[1]), nullptr, 16)) / 255.0f;
            color.g = static_cast<float>(std::stoi(std::string(2, lower[2]), nullptr, 16)) / 255.0f;
            color.b = static_cast<float>(std::stoi(std::string(2, lower[3]), nullptr, 16)) / 255.0f;
            color.a = 1.0f;
            return color;
        }
        if (lower.size() == 5) {
            Color color;
            color.is_valid = true;
            color.r = static_cast<float>(std::stoi(std::string(2, lower[1]), nullptr, 16)) / 255.0f;
            color.g = static_cast<float>(std::stoi(std::string(2, lower[2]), nullptr, 16)) / 255.0f;
            color.b = static_cast<float>(std::stoi(std::string(2, lower[3]), nullptr, 16)) / 255.0f;
            color.a = static_cast<float>(std::stoi(std::string(2, lower[4]), nullptr, 16)) / 255.0f;
            return color;
        }
    }

    if (lower.rfind("rgb(", 0) == 0 || lower.rfind("rgba(", 0) == 0) {
        return ParseFunctionColor(lower);
    }

    if (lower == "transparent") {
        return NamedColor(0.0f, 0.0f, 0.0f, 0.0f);
    }

    if (lower == "black") {
        return NamedColor(0.0f, 0.0f, 0.0f);
    }
    if (lower == "white") {
        return NamedColor(1.0f, 1.0f, 1.0f);
    }
    if (lower == "red") {
        return NamedColor(1.0f, 0.0f, 0.0f);
    }
    if (lower == "green") {
        return NamedColor(0.0f, 0.5f, 0.0f);
    }
    if (lower == "blue") {
        return NamedColor(0.0f, 0.0f, 1.0f);
    }
    if (lower == "yellow") {
        return NamedColor(1.0f, 1.0f, 0.0f);
    }
    if (lower == "orange") {
        return NamedColor(1.0f, 0.64705884f, 0.0f);
    }
    if (lower == "purple") {
        return NamedColor(0.5f, 0.0f, 0.5f);
    }
    if (lower == "gray" || lower == "grey") {
        return NamedColor(0.5f, 0.5f, 0.5f);
    }
    if (lower == "cyan" || lower == "aqua") {
        return NamedColor(0.0f, 1.0f, 1.0f);
    }
    if (lower == "magenta" || lower == "fuchsia") {
        return NamedColor(1.0f, 0.0f, 1.0f);
    }

    return {};
}

ResolvedStyle StyleResolver::Resolve(const XmlNode& node, const ResolvedStyle* parent, const RenderOptions& options) const {
    ResolvedStyle style;
    if (parent != nullptr) {
        style = *parent;
    } else {
        style.color.is_none = false;
        style.color.is_valid = true;
        style.color.r = 0.0f;
        style.color.g = 0.0f;
        style.color.b = 0.0f;
        style.color.a = 1.0f;

        style.fill.is_none = false;
        style.fill.is_valid = true;
        style.fill.r = 0.0f;
        style.fill.g = 0.0f;
        style.fill.b = 0.0f;
        style.fill.a = 1.0f;

        style.stroke.is_none = true;
        style.stroke.is_valid = true;
        style.stroke.r = 0.0f;
        style.stroke.g = 0.0f;
        style.stroke.b = 0.0f;
        style.stroke.a = 1.0f;

        style.color_paint = "black";
        style.fill_paint = "black";
        style.stroke_paint = "none";

        style.fill_opacity = 1.0f;
        style.stroke_opacity = 1.0f;
        style.stroke_width = 1.0f;
        style.opacity = 1.0f;

        style.fill_rule = "nonzero";
        style.stroke_line_cap = "butt";
        style.stroke_line_join = "miter";
        style.stroke_miter_limit = 4.0f;
        style.stroke_dashoffset = 0.0f;
        style.stroke_dasharray.clear();

        style.font_family = options.default_font_family;
        style.font_size = options.default_font_size;
    }

    const auto style_it = node.attributes.find("style");
    const auto inline_style = style_it != node.attributes.end() ? ParseInlineStyle(style_it->second) : std::map<std::string, std::string>{};

    auto read_value = [&](const std::string& key) -> std::optional<std::string> {
        const auto attr_it = node.attributes.find(key);
        if (attr_it != node.attributes.end()) {
            return attr_it->second;
        }
        const auto css_it = inline_style.find(key);
        if (css_it != inline_style.end()) {
            return css_it->second;
        }
        return std::nullopt;
    };

    const auto color = read_value("color");
    const auto fill = read_value("fill");
    const auto fill_rule = read_value("fill-rule");

    if (color.has_value()) {
        style.color_paint = Trim(*color);
        const auto parsed = ParseColor(*color);
        if (parsed.is_valid && !parsed.is_none) {
            style.color = parsed;
        }
    }
    if (fill.has_value()) {
        style.fill_paint = Trim(*fill);
        style.fill = ParseColor(*fill);
    }
    if (const auto stroke = read_value("stroke"); stroke.has_value()) {
        style.stroke_paint = Trim(*stroke);
        style.stroke = ParseColor(*stroke);
    }
    if (const auto fill_opacity = read_value("fill-opacity"); fill_opacity.has_value()) {
        style.fill_opacity = ParseFloat(*fill_opacity, style.fill_opacity);
    }
    if (const auto stroke_opacity = read_value("stroke-opacity"); stroke_opacity.has_value()) {
        style.stroke_opacity = ParseFloat(*stroke_opacity, style.stroke_opacity);
    }
    if (const auto stroke_width = read_value("stroke-width"); stroke_width.has_value()) {
        style.stroke_width = ParseFloat(*stroke_width, style.stroke_width);
    }
    if (const auto opacity = read_value("opacity"); opacity.has_value()) {
        style.opacity = ParseFloat(*opacity, style.opacity);
    }
    if (fill_rule.has_value()) {
        style.fill_rule = Lower(Trim(*fill_rule));
    }
    if (const auto stroke_line_join = read_value("stroke-linejoin"); stroke_line_join.has_value()) {
        style.stroke_line_join = Lower(Trim(*stroke_line_join));
    }
    if (const auto stroke_line_cap = read_value("stroke-linecap"); stroke_line_cap.has_value()) {
        style.stroke_line_cap = Lower(Trim(*stroke_line_cap));
    }
    if (const auto stroke_miter_limit = read_value("stroke-miterlimit"); stroke_miter_limit.has_value()) {
        style.stroke_miter_limit = ParseFloat(*stroke_miter_limit, style.stroke_miter_limit);
    }
    if (const auto stroke_dasharray = read_value("stroke-dasharray"); stroke_dasharray.has_value()) {
        style.stroke_dasharray = ParseFloatList(*stroke_dasharray);
    }
    if (const auto stroke_dashoffset = read_value("stroke-dashoffset"); stroke_dashoffset.has_value()) {
        style.stroke_dashoffset = ParseFloat(*stroke_dashoffset, style.stroke_dashoffset);
    }
    if (const auto font_family = read_value("font-family"); font_family.has_value()) {
        style.font_family = *font_family;
    }
    if (const auto font_size = read_value("font-size"); font_size.has_value()) {
        style.font_size = ParseFloat(*font_size, style.font_size);
    }

    style.fill_opacity = std::clamp(style.fill_opacity, 0.0f, 1.0f);
    style.stroke_opacity = std::clamp(style.stroke_opacity, 0.0f, 1.0f);
    style.opacity = std::clamp(style.opacity, 0.0f, 1.0f);

    if (Lower(Trim(style.fill_paint)) == "currentcolor") {
        style.fill = style.color;
        style.fill.is_none = false;
        style.fill.is_valid = true;
    }
    if (Lower(Trim(style.stroke_paint)) == "currentcolor") {
        style.stroke = style.color;
        style.stroke.is_none = false;
        style.stroke.is_valid = true;
    }

    return style;
}

} // namespace csvg
