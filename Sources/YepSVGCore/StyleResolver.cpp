#include "YepSVGCore/StyleResolver.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
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

float ConvertAbsoluteLength(float value, const std::string& unit) {
    if (unit.empty() || unit == "px") {
        return value;
    }
    if (unit == "pt") {
        return value * (96.0f / 72.0f);
    }
    if (unit == "pc") {
        return value * 16.0f;
    }
    if (unit == "in") {
        return value * 96.0f;
    }
    if (unit == "cm") {
        return value * (96.0f / 2.54f);
    }
    if (unit == "mm") {
        return value * (96.0f / 25.4f);
    }
    if (unit == "q") {
        return value * (96.0f / 101.6f);
    }
    return value;
}

float ParseLength(const std::string& value, float fallback, float percent_basis) {
    const auto trimmed = Trim(value);
    if (trimmed.empty()) {
        return fallback;
    }

    char* end_ptr = nullptr;
    const float parsed = std::strtof(trimmed.c_str(), &end_ptr);
    if (end_ptr == trimmed.c_str()) {
        return fallback;
    }

    const auto unit = Lower(Trim(std::string(end_ptr)));
    if (unit == "%") {
        return parsed * 0.01f * percent_basis;
    }
    if (unit == "em") {
        return parsed * percent_basis;
    }
    if (unit == "ex") {
        return parsed * percent_basis * 0.5f;
    }
    return ConvertAbsoluteLength(parsed, unit);
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

std::optional<Color> ParseNamedColor(const std::string& lower) {
    static const std::unordered_map<std::string, Color> kNamedColors = {
        {"black", NamedColor(0.0f, 0.0f, 0.0f)},
        {"white", NamedColor(1.0f, 1.0f, 1.0f)},
        {"red", NamedColor(1.0f, 0.0f, 0.0f)},
        {"green", NamedColor(0.0f, 0.5f, 0.0f)},
        {"blue", NamedColor(0.0f, 0.0f, 1.0f)},
        {"yellow", NamedColor(1.0f, 1.0f, 0.0f)},
        {"orange", NamedColor(1.0f, 0.64705884f, 0.0f)},
        {"purple", NamedColor(0.5f, 0.0f, 0.5f)},
        {"gray", NamedColor(0.5f, 0.5f, 0.5f)},
        {"grey", NamedColor(0.5f, 0.5f, 0.5f)},
        {"cyan", NamedColor(0.0f, 1.0f, 1.0f)},
        {"aqua", NamedColor(0.0f, 1.0f, 1.0f)},
        {"magenta", NamedColor(1.0f, 0.0f, 1.0f)},
        {"fuchsia", NamedColor(1.0f, 0.0f, 1.0f)},
        {"lime", NamedColor(0.0f, 1.0f, 0.0f)},
        {"navy", NamedColor(0.0f, 0.0f, 0.5019608f)},
        {"maroon", NamedColor(0.5019608f, 0.0f, 0.0f)},
        {"silver", NamedColor(0.7529412f, 0.7529412f, 0.7529412f)},
        {"gold", NamedColor(1.0f, 0.84313726f, 0.0f)},
        {"darkblue", NamedColor(0.0f, 0.0f, 0.54509807f)},
        {"lightblue", NamedColor(0.6784314f, 0.84705883f, 0.9019608f)},
        {"forestgreen", NamedColor(0.13333334f, 0.54509807f, 0.13333334f)},
        {"crimson", NamedColor(0.8627451f, 0.078431375f, 0.23529412f)},
        {"palegreen", NamedColor(0.59607846f, 0.9843137f, 0.59607846f)},
        {"royalblue", NamedColor(0.25490198f, 0.4117647f, 0.88235295f)},
        {"firebrick", NamedColor(0.69803923f, 0.13333334f, 0.13333334f)},
        {"seagreen", NamedColor(0.18039216f, 0.54509807f, 0.34117648f)},
        {"mediumblue", NamedColor(0.0f, 0.0f, 0.8039216f)},
        {"indianred", NamedColor(0.8039216f, 0.36078432f, 0.36078432f)},
        {"lawngreen", NamedColor(0.4862745f, 0.9882353f, 0.0f)},
        {"mediumturquoise", NamedColor(0.28235295f, 0.81960785f, 0.8f)},
        // CSS/SVG system color keywords (legacy) used by W3C color-prop fixtures.
        {"activeborder", NamedColor(0.0f, 0.0f, 0.0f)},
        {"activecaption", NamedColor(0.0f, 0.0f, 0.5019608f)},
        {"appworkspace", NamedColor(0.0f, 0.36078432f, 0.36078432f)},
        {"background", NamedColor(0.0f, 0.36078432f, 0.36078432f)},
        {"buttonface", NamedColor(0.7529412f, 0.7529412f, 0.7529412f)},
        {"buttonhighlight", NamedColor(1.0f, 1.0f, 1.0f)},
        {"buttonshadow", NamedColor(0.627451f, 0.627451f, 0.627451f)},
        {"buttontext", NamedColor(0.0f, 0.0f, 0.0f)},
        {"captiontext", NamedColor(1.0f, 1.0f, 1.0f)},
        {"graytext", NamedColor(0.427451f, 0.427451f, 0.427451f)},
        {"highlight", NamedColor(0.0f, 0.0f, 0.5019608f)},
        {"highlighttext", NamedColor(1.0f, 1.0f, 1.0f)},
        {"inactiveborder", NamedColor(0.7529412f, 0.7529412f, 0.7529412f)},
        {"inactivecaption", NamedColor(0.5019608f, 0.5019608f, 0.5019608f)},
        {"inactivecaptiontext", NamedColor(0.7529412f, 0.7529412f, 0.7529412f)},
        {"infobackground", NamedColor(1.0f, 1.0f, 0.88235295f)},
        {"infotext", NamedColor(0.0f, 0.0f, 0.0f)},
        {"menu", NamedColor(0.7529412f, 0.7529412f, 0.7529412f)},
        {"menutext", NamedColor(0.0f, 0.0f, 0.0f)},
        {"scrollbar", NamedColor(0.78431374f, 0.78431374f, 0.78431374f)},
        {"threeddarkshadow", NamedColor(0.0f, 0.0f, 0.0f)},
        {"threedface", NamedColor(0.7529412f, 0.7529412f, 0.7529412f)},
        {"threedhighlight", NamedColor(1.0f, 1.0f, 1.0f)},
        {"threedlightshadow", NamedColor(0.8901961f, 0.8901961f, 0.8901961f)},
        {"threedshadow", NamedColor(0.627451f, 0.627451f, 0.627451f)},
        {"window", NamedColor(1.0f, 1.0f, 1.0f)},
        {"windowframe", NamedColor(0.0f, 0.0f, 0.0f)},
        {"windowtext", NamedColor(0.0f, 0.0f, 0.0f)},
    };

    const auto it = kNamedColors.find(lower);
    if (it == kNamedColors.end()) {
        return std::nullopt;
    }
    return it->second;
}

int ParseFontWeight(const std::string& value, int fallback) {
    const auto trimmed = Lower(Trim(value));
    if (trimmed.empty()) {
        return fallback;
    }
    if (trimmed == "normal") {
        return 400;
    }
    if (trimmed == "bold" || trimmed == "bolder") {
        return 700;
    }
    if (trimmed == "lighter") {
        return 300;
    }

    const auto numeric = ParseCssFloat(trimmed);
    if (!numeric.has_value()) {
        return fallback;
    }
    return static_cast<int>(std::clamp(*numeric, 1.0f, 1000.0f));
}

std::vector<std::string> SplitWhitespacePreservingQuotes(const std::string& value) {
    std::vector<std::string> tokens;
    std::string current;
    char quote = '\0';
    for (char c : value) {
        if (quote != '\0') {
            current.push_back(c);
            if (c == quote) {
                quote = '\0';
            }
            continue;
        }
        if (c == '"' || c == '\'') {
            quote = c;
            current.push_back(c);
            continue;
        }
        if (std::isspace(static_cast<unsigned char>(c)) != 0) {
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

struct FontShorthand {
    bool has_size = false;
    float size = 0.0f;
    std::string family;
    std::optional<std::string> style;
    std::optional<int> weight;
};

std::optional<FontShorthand> ParseFontShorthand(const std::string& value, float inherited_size) {
    const std::string trimmed = Trim(value);
    if (trimmed.empty()) {
        return std::nullopt;
    }

    const auto tokens = SplitWhitespacePreservingQuotes(trimmed);
    if (tokens.empty()) {
        return std::nullopt;
    }

    FontShorthand shorthand;
    size_t size_index = tokens.size();
    std::string size_token;
    for (size_t index = 0; index < tokens.size(); ++index) {
        std::string candidate = tokens[index];
        const auto slash = candidate.find('/');
        if (slash != std::string::npos) {
            candidate = candidate.substr(0, slash);
        }
        const float parsed = ParseLength(candidate, -1.0f, inherited_size);
        if (parsed > 0.0f) {
            shorthand.has_size = true;
            shorthand.size = parsed;
            size_index = index;
            size_token = tokens[index];
            break;
        }
    }
    if (!shorthand.has_size || size_index + 1 >= tokens.size()) {
        return std::nullopt;
    }

    for (size_t index = 0; index < size_index; ++index) {
        const std::string token = Lower(Trim(tokens[index]));
        if (token == "italic" || token == "oblique" || token == "normal") {
            shorthand.style = token;
            continue;
        }
        const int parsed_weight = ParseFontWeight(token, -1);
        if (parsed_weight > 0) {
            shorthand.weight = parsed_weight;
        }
    }

    std::string family;
    for (size_t index = size_index + 1; index < tokens.size(); ++index) {
        if (!family.empty()) {
            family.push_back(' ');
        }
        family += tokens[index];
    }
    shorthand.family = Trim(family);
    if (shorthand.family.empty()) {
        return std::nullopt;
    }
    return shorthand;
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

    if (const auto named = ParseNamedColor(lower); named.has_value()) {
        return *named;
    }

    return {};
}

ResolvedStyle StyleResolver::Resolve(const XmlNode& node,
                                     const ResolvedStyle* parent,
                                     const RenderOptions& options,
                                     const std::map<std::string, std::string>* matched_css_properties) const {
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
        style.font_weight = 400;
        style.font_style = "normal";
        style.text_decoration = "none";
        style.letter_spacing = 0.0f;
        style.word_spacing = 0.0f;
        style.text_anchor = "start";
    }

    const auto style_it = node.attributes.find("style");
    const auto inline_style = style_it != node.attributes.end() ? ParseInlineStyle(style_it->second) : std::map<std::string, std::string>{};

    auto read_value = [&](const std::string& key) -> std::optional<std::string> {
        const auto inline_it = inline_style.find(key);
        if (inline_it != inline_style.end()) {
            return inline_it->second;
        }
        if (matched_css_properties != nullptr) {
            const auto matched_it = matched_css_properties->find(key);
            if (matched_it != matched_css_properties->end()) {
                return matched_it->second;
            }
        }
        const auto attr_it = node.attributes.find(key);
        if (attr_it != node.attributes.end()) {
            return attr_it->second;
        }
        return std::nullopt;
    };

    const auto color = read_value("color");
    const auto fill = read_value("fill");
    const bool has_local_fill = fill.has_value();
    const auto fill_rule = read_value("fill-rule");
    const auto stroke = read_value("stroke");
    const bool has_local_stroke = stroke.has_value();

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
    if (stroke.has_value()) {
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
        style.stroke_width = ParseLength(*stroke_width, style.stroke_width, style.font_size);
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
        style.stroke_dashoffset = ParseLength(*stroke_dashoffset, style.stroke_dashoffset, style.font_size);
    }
    if (const auto font_family = read_value("font-family"); font_family.has_value()) {
        style.font_family = *font_family;
    }
    if (const auto font_shorthand = read_value("font"); font_shorthand.has_value()) {
        if (const auto parsed = ParseFontShorthand(*font_shorthand, style.font_size); parsed.has_value()) {
            style.font_size = parsed->size;
            style.font_family = parsed->family;
            if (parsed->style.has_value()) {
                style.font_style = *parsed->style;
            }
            if (parsed->weight.has_value()) {
                style.font_weight = *parsed->weight;
            }
        }
    }
    if (const auto font_size = read_value("font-size"); font_size.has_value()) {
        style.font_size = ParseLength(*font_size, style.font_size, style.font_size);
    }
    if (const auto font_weight = read_value("font-weight"); font_weight.has_value()) {
        style.font_weight = ParseFontWeight(*font_weight, style.font_weight);
    }
    if (const auto font_style = read_value("font-style"); font_style.has_value()) {
        const auto parsed = Lower(Trim(*font_style));
        if (parsed == "normal" || parsed == "italic" || parsed == "oblique") {
            style.font_style = parsed;
        }
    }
    if (const auto text_decoration = read_value("text-decoration"); text_decoration.has_value()) {
        const auto parsed = Lower(Trim(*text_decoration));
        style.text_decoration = parsed.empty() ? "none" : parsed;
    }
    if (const auto letter_spacing = read_value("letter-spacing"); letter_spacing.has_value()) {
        const auto parsed = Lower(Trim(*letter_spacing));
        if (parsed == "normal") {
            style.letter_spacing = 0.0f;
        } else {
            style.letter_spacing = ParseLength(*letter_spacing, style.letter_spacing, style.font_size);
        }
    }
    if (const auto word_spacing = read_value("word-spacing"); word_spacing.has_value()) {
        const auto parsed = Lower(Trim(*word_spacing));
        if (parsed == "normal") {
            style.word_spacing = 0.0f;
        } else {
            style.word_spacing = ParseLength(*word_spacing, style.word_spacing, style.font_size);
        }
    }
    if (const auto text_anchor = read_value("text-anchor"); text_anchor.has_value()) {
        const auto anchor = Lower(Trim(*text_anchor));
        if (anchor == "start" || anchor == "middle" || anchor == "end") {
            style.text_anchor = anchor;
        }
    }

    style.fill_opacity = std::clamp(style.fill_opacity, 0.0f, 1.0f);
    style.stroke_opacity = std::clamp(style.stroke_opacity, 0.0f, 1.0f);
    style.opacity = std::clamp(style.opacity, 0.0f, 1.0f);

    if (has_local_fill && Lower(Trim(style.fill_paint)) == "currentcolor") {
        style.fill = style.color;
        style.fill.is_none = false;
        style.fill.is_valid = true;
    }
    if (has_local_stroke && Lower(Trim(style.stroke_paint)) == "currentcolor") {
        style.stroke = style.color;
        style.stroke.is_none = false;
        style.stroke.is_valid = true;
    }

    return style;
}

} // namespace csvg
