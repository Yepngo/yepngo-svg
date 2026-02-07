#include "YepSVGCore/PaintEngine.hpp"

#include <ImageIO/ImageIO.h>
#include <CoreText/CoreText.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <set>
#include <map>
#include <numeric>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace csvg {
namespace {

struct GradientStop {
    double offset = 0.0;
    Color color;
    double opacity = 1.0;
};

enum class GradientType {
    kLinear,
    kRadial,
};

struct GradientDefinition {
    GradientType type = GradientType::kLinear;
    std::string id;
    bool user_space_units = false;
    CGAffineTransform transform = CGAffineTransformIdentity;

    double x1 = 0.0;
    double y1 = 0.0;
    double x2 = 1.0;
    double y2 = 0.0;

    double cx = 0.5;
    double cy = 0.5;
    double r = 0.5;
    double fx = 0.5;
    double fy = 0.5;

    std::vector<GradientStop> stops;
};

struct PatternDefinition {
    std::string id;
    bool pattern_units_user_space = false;
    bool content_units_user_space = true;
    CGAffineTransform transform = CGAffineTransformIdentity;
    double x = 0.0;
    double y = 0.0;
    double width = 0.0;
    double height = 0.0;
    const XmlNode* node = nullptr;
};

using GradientMap = std::map<std::string, GradientDefinition>;
using PatternMap = std::map<std::string, PatternDefinition>;
using NodeIdMap = std::map<std::string, const XmlNode*>;
using ColorProfileMap = std::map<std::string, std::string>;

std::string Trim(const std::string& value) {
    const auto begin = value.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) {
        return "";
    }
    const auto end = value.find_last_not_of(" \t\r\n");
    return value.substr(begin, end - begin + 1);
}

std::string Lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

bool IsNumberStart(char c) {
    return std::isdigit(static_cast<unsigned char>(c)) || c == '-' || c == '+' || c == '.';
}

void SkipPathDelimiters(const std::string& text, size_t& index) {
    while (index < text.size()) {
        const char c = text[index];
        if (std::isspace(static_cast<unsigned char>(c)) || c == ',') {
            ++index;
            continue;
        }
        break;
    }
}

bool ParsePathNumber(const std::string& text, size_t& index, double& out) {
    SkipPathDelimiters(text, index);
    if (index >= text.size()) {
        return false;
    }

    const char* start = text.c_str() + index;
    char* end_ptr = nullptr;
    out = std::strtod(start, &end_ptr);
    if (end_ptr == start) {
        return false;
    }
    index = static_cast<size_t>(end_ptr - text.c_str());
    return true;
}

bool HasMorePathNumbers(const std::string& text, size_t index) {
    SkipPathDelimiters(text, index);
    if (index >= text.size()) {
        return false;
    }
    return IsNumberStart(text[index]);
}

constexpr double kArcEpsilon = 1e-9;

bool NearlyEqual(double a, double b) {
    return std::fabs(a - b) <= kArcEpsilon;
}

double SignedVectorAngle(double ux, double uy, double vx, double vy) {
    return std::atan2((ux * vy) - (uy * vx), (ux * vx) + (uy * vy));
}

CGPoint MapUnitArcPoint(double ux,
                        double uy,
                        double cx,
                        double cy,
                        double rx,
                        double ry,
                        double cos_phi,
                        double sin_phi) {
    const double x = cx + (rx * ux * cos_phi) - (ry * uy * sin_phi);
    const double y = cy + (rx * ux * sin_phi) + (ry * uy * cos_phi);
    return CGPointMake(static_cast<CGFloat>(x), static_cast<CGFloat>(y));
}

void AddArcAsBezier(CGContextRef context,
                    double x1,
                    double y1,
                    double rx,
                    double ry,
                    double x_axis_rotation_degrees,
                    bool large_arc_flag,
                    bool sweep_flag,
                    double x2,
                    double y2) {
    if (NearlyEqual(x1, x2) && NearlyEqual(y1, y2)) {
        return;
    }

    rx = std::fabs(rx);
    ry = std::fabs(ry);
    if (rx < kArcEpsilon || ry < kArcEpsilon) {
        CGContextAddLineToPoint(context, static_cast<CGFloat>(x2), static_cast<CGFloat>(y2));
        return;
    }

    const double phi = x_axis_rotation_degrees * M_PI / 180.0;
    const double cos_phi = std::cos(phi);
    const double sin_phi = std::sin(phi);

    const double dx2 = (x1 - x2) / 2.0;
    const double dy2 = (y1 - y2) / 2.0;
    const double x1p = (cos_phi * dx2) + (sin_phi * dy2);
    const double y1p = (-sin_phi * dx2) + (cos_phi * dy2);

    const double x1p2 = x1p * x1p;
    const double y1p2 = y1p * y1p;

    double rx2 = rx * rx;
    double ry2 = ry * ry;
    const double lambda = (x1p2 / rx2) + (y1p2 / ry2);
    if (lambda > 1.0) {
        const double scale = std::sqrt(lambda);
        rx *= scale;
        ry *= scale;
        rx2 = rx * rx;
        ry2 = ry * ry;
    }

    const double numerator = (rx2 * ry2) - (rx2 * y1p2) - (ry2 * x1p2);
    const double denominator = (rx2 * y1p2) + (ry2 * x1p2);
    if (std::fabs(denominator) < kArcEpsilon) {
        CGContextAddLineToPoint(context, static_cast<CGFloat>(x2), static_cast<CGFloat>(y2));
        return;
    }

    double center_scale = std::sqrt(std::max(0.0, numerator / denominator));
    if (large_arc_flag == sweep_flag) {
        center_scale = -center_scale;
    }

    const double cxp = center_scale * ((rx * y1p) / ry);
    const double cyp = center_scale * (-(ry * x1p) / rx);

    const double cx = (cos_phi * cxp) - (sin_phi * cyp) + ((x1 + x2) / 2.0);
    const double cy = (sin_phi * cxp) + (cos_phi * cyp) + ((y1 + y2) / 2.0);

    const double ux = (x1p - cxp) / rx;
    const double uy = (y1p - cyp) / ry;
    const double vx = (-x1p - cxp) / rx;
    const double vy = (-y1p - cyp) / ry;

    double start_angle = std::atan2(uy, ux);
    double delta_angle = SignedVectorAngle(ux, uy, vx, vy);
    if (!sweep_flag && delta_angle > 0.0) {
        delta_angle -= 2.0 * M_PI;
    } else if (sweep_flag && delta_angle < 0.0) {
        delta_angle += 2.0 * M_PI;
    }

    const int segment_count = std::max(1, static_cast<int>(std::ceil(std::fabs(delta_angle) / (M_PI / 2.0))));
    const double segment_angle = delta_angle / static_cast<double>(segment_count);

    for (int i = 0; i < segment_count; ++i) {
        const double theta1 = start_angle + (segment_angle * static_cast<double>(i));
        const double theta2 = theta1 + segment_angle;
        const double alpha = (4.0 / 3.0) * std::tan((theta2 - theta1) / 4.0);

        const double cos_theta1 = std::cos(theta1);
        const double sin_theta1 = std::sin(theta1);
        const double cos_theta2 = std::cos(theta2);
        const double sin_theta2 = std::sin(theta2);

        const double cp1x_u = cos_theta1 - (alpha * sin_theta1);
        const double cp1y_u = sin_theta1 + (alpha * cos_theta1);
        const double cp2x_u = cos_theta2 + (alpha * sin_theta2);
        const double cp2y_u = sin_theta2 - (alpha * cos_theta2);

        const CGPoint cp1 = MapUnitArcPoint(cp1x_u, cp1y_u, cx, cy, rx, ry, cos_phi, sin_phi);
        const CGPoint cp2 = MapUnitArcPoint(cp2x_u, cp2y_u, cx, cy, rx, ry, cos_phi, sin_phi);
        const CGPoint end = MapUnitArcPoint(cos_theta2, sin_theta2, cx, cy, rx, ry, cos_phi, sin_phi);

        CGContextAddCurveToPoint(context, cp1.x, cp1.y, cp2.x, cp2.y, end.x, end.y);
    }
}

bool BuildPathFromData(CGContextRef context, const std::string& path_data) {
    size_t index = 0;
    char command = '\0';

    double current_x = 0.0;
    double current_y = 0.0;
    double start_x = 0.0;
    double start_y = 0.0;

    double last_cubic_ctrl_x = 0.0;
    double last_cubic_ctrl_y = 0.0;
    double last_quad_ctrl_x = 0.0;
    double last_quad_ctrl_y = 0.0;
    bool has_last_cubic = false;
    bool has_last_quad = false;

    while (index < path_data.size()) {
        SkipPathDelimiters(path_data, index);
        if (index >= path_data.size()) {
            break;
        }

        const char token = path_data[index];
        if (std::isalpha(static_cast<unsigned char>(token))) {
            command = token;
            ++index;
        } else if (command == '\0') {
            ++index;
            continue;
        }

        const bool relative = std::islower(static_cast<unsigned char>(command));
        const char upper = static_cast<char>(std::toupper(static_cast<unsigned char>(command)));

        if (upper == 'M') {
            double x = 0.0;
            double y = 0.0;
            if (!ParsePathNumber(path_data, index, x) || !ParsePathNumber(path_data, index, y)) {
                break;
            }
            if (relative) {
                x += current_x;
                y += current_y;
            }
            CGContextMoveToPoint(context, static_cast<CGFloat>(x), static_cast<CGFloat>(y));
            current_x = x;
            current_y = y;
            start_x = x;
            start_y = y;

            while (HasMorePathNumbers(path_data, index)) {
                if (!ParsePathNumber(path_data, index, x) || !ParsePathNumber(path_data, index, y)) {
                    break;
                }
                if (relative) {
                    x += current_x;
                    y += current_y;
                }
                CGContextAddLineToPoint(context, static_cast<CGFloat>(x), static_cast<CGFloat>(y));
                current_x = x;
                current_y = y;
            }
            has_last_cubic = false;
            has_last_quad = false;
            continue;
        }

        if (upper == 'L') {
            double x = 0.0;
            double y = 0.0;
            while (ParsePathNumber(path_data, index, x) && ParsePathNumber(path_data, index, y)) {
                if (relative) {
                    x += current_x;
                    y += current_y;
                }
                CGContextAddLineToPoint(context, static_cast<CGFloat>(x), static_cast<CGFloat>(y));
                current_x = x;
                current_y = y;
            }
            has_last_cubic = false;
            has_last_quad = false;
            continue;
        }

        if (upper == 'H') {
            double x = 0.0;
            while (ParsePathNumber(path_data, index, x)) {
                if (relative) {
                    x += current_x;
                }
                CGContextAddLineToPoint(context, static_cast<CGFloat>(x), static_cast<CGFloat>(current_y));
                current_x = x;
            }
            has_last_cubic = false;
            has_last_quad = false;
            continue;
        }

        if (upper == 'V') {
            double y = 0.0;
            while (ParsePathNumber(path_data, index, y)) {
                if (relative) {
                    y += current_y;
                }
                CGContextAddLineToPoint(context, static_cast<CGFloat>(current_x), static_cast<CGFloat>(y));
                current_y = y;
            }
            has_last_cubic = false;
            has_last_quad = false;
            continue;
        }

        if (upper == 'C') {
            double x1 = 0.0;
            double y1 = 0.0;
            double x2 = 0.0;
            double y2 = 0.0;
            double x = 0.0;
            double y = 0.0;
            while (ParsePathNumber(path_data, index, x1) &&
                   ParsePathNumber(path_data, index, y1) &&
                   ParsePathNumber(path_data, index, x2) &&
                   ParsePathNumber(path_data, index, y2) &&
                   ParsePathNumber(path_data, index, x) &&
                   ParsePathNumber(path_data, index, y)) {
                if (relative) {
                    x1 += current_x;
                    y1 += current_y;
                    x2 += current_x;
                    y2 += current_y;
                    x += current_x;
                    y += current_y;
                }
                CGContextAddCurveToPoint(context,
                                         static_cast<CGFloat>(x1),
                                         static_cast<CGFloat>(y1),
                                         static_cast<CGFloat>(x2),
                                         static_cast<CGFloat>(y2),
                                         static_cast<CGFloat>(x),
                                         static_cast<CGFloat>(y));
                current_x = x;
                current_y = y;
                last_cubic_ctrl_x = x2;
                last_cubic_ctrl_y = y2;
                has_last_cubic = true;
                has_last_quad = false;
            }
            continue;
        }

        if (upper == 'S') {
            double x2 = 0.0;
            double y2 = 0.0;
            double x = 0.0;
            double y = 0.0;
            while (ParsePathNumber(path_data, index, x2) &&
                   ParsePathNumber(path_data, index, y2) &&
                   ParsePathNumber(path_data, index, x) &&
                   ParsePathNumber(path_data, index, y)) {
                double x1 = current_x;
                double y1 = current_y;
                if (has_last_cubic) {
                    x1 = 2.0 * current_x - last_cubic_ctrl_x;
                    y1 = 2.0 * current_y - last_cubic_ctrl_y;
                }

                if (relative) {
                    x2 += current_x;
                    y2 += current_y;
                    x += current_x;
                    y += current_y;
                }

                CGContextAddCurveToPoint(context,
                                         static_cast<CGFloat>(x1),
                                         static_cast<CGFloat>(y1),
                                         static_cast<CGFloat>(x2),
                                         static_cast<CGFloat>(y2),
                                         static_cast<CGFloat>(x),
                                         static_cast<CGFloat>(y));

                current_x = x;
                current_y = y;
                last_cubic_ctrl_x = x2;
                last_cubic_ctrl_y = y2;
                has_last_cubic = true;
                has_last_quad = false;
            }
            continue;
        }

        if (upper == 'Q') {
            double x1 = 0.0;
            double y1 = 0.0;
            double x = 0.0;
            double y = 0.0;
            while (ParsePathNumber(path_data, index, x1) &&
                   ParsePathNumber(path_data, index, y1) &&
                   ParsePathNumber(path_data, index, x) &&
                   ParsePathNumber(path_data, index, y)) {
                if (relative) {
                    x1 += current_x;
                    y1 += current_y;
                    x += current_x;
                    y += current_y;
                }

                CGContextAddQuadCurveToPoint(context,
                                             static_cast<CGFloat>(x1),
                                             static_cast<CGFloat>(y1),
                                             static_cast<CGFloat>(x),
                                             static_cast<CGFloat>(y));

                current_x = x;
                current_y = y;
                last_quad_ctrl_x = x1;
                last_quad_ctrl_y = y1;
                has_last_quad = true;
                has_last_cubic = false;
            }
            continue;
        }

        if (upper == 'T') {
            double x = 0.0;
            double y = 0.0;
            while (ParsePathNumber(path_data, index, x) && ParsePathNumber(path_data, index, y)) {
                double x1 = current_x;
                double y1 = current_y;
                if (has_last_quad) {
                    x1 = 2.0 * current_x - last_quad_ctrl_x;
                    y1 = 2.0 * current_y - last_quad_ctrl_y;
                }

                if (relative) {
                    x += current_x;
                    y += current_y;
                }

                CGContextAddQuadCurveToPoint(context,
                                             static_cast<CGFloat>(x1),
                                             static_cast<CGFloat>(y1),
                                             static_cast<CGFloat>(x),
                                             static_cast<CGFloat>(y));

                current_x = x;
                current_y = y;
                last_quad_ctrl_x = x1;
                last_quad_ctrl_y = y1;
                has_last_quad = true;
                has_last_cubic = false;
            }
            continue;
        }

        if (upper == 'A') {
            double rx = 0.0;
            double ry = 0.0;
            double angle = 0.0;
            double large_arc = 0.0;
            double sweep = 0.0;
            double x = 0.0;
            double y = 0.0;
            while (ParsePathNumber(path_data, index, rx) &&
                   ParsePathNumber(path_data, index, ry) &&
                   ParsePathNumber(path_data, index, angle) &&
                   ParsePathNumber(path_data, index, large_arc) &&
                   ParsePathNumber(path_data, index, sweep) &&
                   ParsePathNumber(path_data, index, x) &&
                   ParsePathNumber(path_data, index, y)) {
                if (relative) {
                    x += current_x;
                    y += current_y;
                }

                AddArcAsBezier(context,
                               current_x,
                               current_y,
                               rx,
                               ry,
                               angle,
                               std::fabs(large_arc) > 0.0,
                               std::fabs(sweep) > 0.0,
                               x,
                               y);
                current_x = x;
                current_y = y;
            }
            has_last_cubic = false;
            has_last_quad = false;
            continue;
        }

        if (upper == 'Z') {
            CGContextClosePath(context);
            current_x = start_x;
            current_y = start_y;
            has_last_cubic = false;
            has_last_quad = false;
            continue;
        }

        ++index;
    }

    return true;
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

std::optional<std::string> ReadAttrOrStyle(const XmlNode& node, const std::map<std::string, std::string>& inline_style, const std::string& key) {
    const auto attr_it = node.attributes.find(key);
    if (attr_it != node.attributes.end()) {
        return attr_it->second;
    }
    const auto css_it = inline_style.find(key);
    if (css_it != inline_style.end()) {
        return css_it->second;
    }
    return std::nullopt;
}

double ParseCoordinate(const std::string& raw, double fallback) {
    const auto trimmed = Trim(raw);
    if (trimmed.empty()) {
        return fallback;
    }

    char* end_ptr = nullptr;
    const double parsed = std::strtod(trimmed.c_str(), &end_ptr);
    if (end_ptr == trimmed.c_str()) {
        return fallback;
    }

    if (*end_ptr == '%') {
        return parsed / 100.0;
    }
    return parsed;
}

double ParseOffset(const std::string& raw) {
    const auto trimmed = Trim(raw);
    if (trimmed.empty()) {
        return 0.0;
    }

    char* end_ptr = nullptr;
    const double parsed = std::strtod(trimmed.c_str(), &end_ptr);
    if (end_ptr == trimmed.c_str()) {
        return 0.0;
    }

    if (*end_ptr == '%') {
        return std::clamp(parsed / 100.0, 0.0, 1.0);
    }
    return std::clamp(parsed, 0.0, 1.0);
}

double ParseDouble(const std::string& raw, double fallback) {
    const auto trimmed = Trim(raw);
    if (trimmed.empty()) {
        return fallback;
    }

    char* end_ptr = nullptr;
    const double parsed = std::strtod(trimmed.c_str(), &end_ptr);
    if (end_ptr == trimmed.c_str()) {
        return fallback;
    }

    if (*end_ptr == '%') {
        return parsed / 100.0;
    }
    return parsed;
}

enum class SvgLengthAxis {
    kX,
    kY,
    kDiagonal,
};

double NormalizeViewportDiagonal(double width, double height) {
    if (width <= 0.0 || height <= 0.0) {
        return 100.0;
    }
    return std::sqrt((width * width + height * height) / 2.0);
}

double PercentLengthBasis(SvgLengthAxis axis, double viewport_width, double viewport_height) {
    switch (axis) {
        case SvgLengthAxis::kX:
            return viewport_width > 0.0 ? viewport_width : 100.0;
        case SvgLengthAxis::kY:
            return viewport_height > 0.0 ? viewport_height : 100.0;
        case SvgLengthAxis::kDiagonal:
            return NormalizeViewportDiagonal(viewport_width, viewport_height);
    }
    return 100.0;
}

double ConvertLengthToPixels(double value, const std::string& unit) {
    if (unit.empty() || unit == "px") {
        return value;
    }
    if (unit == "pt") {
        return value * (96.0 / 72.0);
    }
    if (unit == "pc") {
        return value * 16.0;
    }
    if (unit == "in") {
        return value * 96.0;
    }
    if (unit == "cm") {
        return value * (96.0 / 2.54);
    }
    if (unit == "mm") {
        return value * (96.0 / 25.4);
    }
    if (unit == "q") {
        return value * (96.0 / 101.6);
    }
    return value;
}

double ParseSVGLength(const std::string& raw,
                      double fallback,
                      SvgLengthAxis axis,
                      double viewport_width,
                      double viewport_height) {
    const auto trimmed = Trim(raw);
    if (trimmed.empty()) {
        return fallback;
    }

    char* end_ptr = nullptr;
    const double parsed = std::strtod(trimmed.c_str(), &end_ptr);
    if (end_ptr == trimmed.c_str()) {
        return fallback;
    }

    const std::string suffix = Lower(Trim(std::string(end_ptr)));
    if (suffix == "%") {
        const double basis = PercentLengthBasis(axis, viewport_width, viewport_height);
        return (parsed / 100.0) * basis;
    }
    return ConvertLengthToPixels(parsed, suffix);
}

double ParseSVGLengthAttr(const std::map<std::string, std::string>& attributes,
                          const std::string& key,
                          double fallback,
                          SvgLengthAxis axis,
                          double viewport_width,
                          double viewport_height) {
    const auto it = attributes.find(key);
    if (it == attributes.end()) {
        return fallback;
    }
    return ParseSVGLength(it->second, fallback, axis, viewport_width, viewport_height);
}

bool ParseViewBoxValue(const std::string& raw,
                       double& view_box_x,
                       double& view_box_y,
                       double& view_box_width,
                       double& view_box_height) {
    std::string normalized = raw;
    std::replace(normalized.begin(), normalized.end(), ',', ' ');

    std::stringstream parser(normalized);
    if (!(parser >> view_box_x >> view_box_y >> view_box_width >> view_box_height)) {
        return false;
    }
    return view_box_width > 0.0 && view_box_height > 0.0;
}

CGAffineTransform ComputeViewBoxTransform(double viewport_width,
                                          double viewport_height,
                                          double view_box_x,
                                          double view_box_y,
                                          double view_box_width,
                                          double view_box_height,
                                          const std::string& preserve_raw) {
    const double scale_x = viewport_width / view_box_width;
    const double scale_y = viewport_height / view_box_height;

    std::string preserve_value = Lower(Trim(preserve_raw));
    if (preserve_value.empty()) {
        preserve_value = "xmidymid meet";
    }

    double final_scale_x = scale_x;
    double final_scale_y = scale_y;
    double translate_x = -view_box_x * scale_x;
    double translate_y = -view_box_y * scale_y;

    if (preserve_value != "none") {
        std::stringstream parser(preserve_value);
        std::string align = "xmidymid";
        std::string meet_or_slice = "meet";
        parser >> align >> meet_or_slice;
        if (align == "defer") {
            parser >> align >> meet_or_slice;
        }
        if (align.empty()) {
            align = "xmidymid";
        }
        if (meet_or_slice != "slice") {
            meet_or_slice = "meet";
        }

        const double uniform_scale = meet_or_slice == "slice"
            ? std::max(scale_x, scale_y)
            : std::min(scale_x, scale_y);
        final_scale_x = uniform_scale;
        final_scale_y = uniform_scale;

        const double content_width = view_box_width * uniform_scale;
        const double content_height = view_box_height * uniform_scale;
        const double extra_x = viewport_width - content_width;
        const double extra_y = viewport_height - content_height;

        double align_x = 0.0;
        double align_y = 0.0;
        if (align.find("xmid") != std::string::npos) {
            align_x = extra_x * 0.5;
        } else if (align.find("xmax") != std::string::npos) {
            align_x = extra_x;
        }
        if (align.find("ymid") != std::string::npos) {
            align_y = extra_y * 0.5;
        } else if (align.find("ymax") != std::string::npos) {
            align_y = extra_y;
        }

        translate_x = align_x - (view_box_x * uniform_scale);
        translate_y = align_y - (view_box_y * uniform_scale);
    }

    return CGAffineTransformMake(static_cast<CGFloat>(final_scale_x),
                                 0.0,
                                 0.0,
                                 static_cast<CGFloat>(final_scale_y),
                                 static_cast<CGFloat>(translate_x),
                                 static_cast<CGFloat>(translate_y));
}

struct PreserveAspectRatioSpec {
    bool none = false;
    bool slice = false;
    double align_x = 0.5;
    double align_y = 0.5;
};

PreserveAspectRatioSpec ParsePreserveAspectRatioSpec(const std::string& preserve_raw) {
    PreserveAspectRatioSpec spec;
    std::string preserve_value = Lower(Trim(preserve_raw));
    if (preserve_value.empty()) {
        preserve_value = "xmidymid meet";
    }

    std::stringstream parser(preserve_value);
    std::string align = "xmidymid";
    std::string meet_or_slice = "meet";
    parser >> align >> meet_or_slice;
    if (align == "defer") {
        parser >> align >> meet_or_slice;
    }
    if (align.empty()) {
        align = "xmidymid";
    }

    if (align == "none") {
        spec.none = true;
        spec.slice = false;
        spec.align_x = 0.0;
        spec.align_y = 0.0;
        return spec;
    }

    spec.slice = meet_or_slice == "slice";
    spec.align_x = 0.0;
    spec.align_y = 0.0;
    if (align.find("xmid") != std::string::npos) {
        spec.align_x = 0.5;
    } else if (align.find("xmax") != std::string::npos) {
        spec.align_x = 1.0;
    }
    if (align.find("ymid") != std::string::npos) {
        spec.align_y = 0.5;
    } else if (align.find("ymax") != std::string::npos) {
        spec.align_y = 1.0;
    }
    return spec;
}

bool IsHexDigit(char c) {
    return (c >= '0' && c <= '9') ||
           (c >= 'a' && c <= 'f') ||
           (c >= 'A' && c <= 'F');
}

uint8_t HexValue(char c) {
    if (c >= '0' && c <= '9') {
        return static_cast<uint8_t>(c - '0');
    }
    if (c >= 'a' && c <= 'f') {
        return static_cast<uint8_t>(10 + (c - 'a'));
    }
    return static_cast<uint8_t>(10 + (c - 'A'));
}

std::string PercentDecode(const std::string& value) {
    std::string decoded;
    decoded.reserve(value.size());
    for (size_t i = 0; i < value.size(); ++i) {
        const char c = value[i];
        if (c == '%' && i + 2 < value.size() && IsHexDigit(value[i + 1]) && IsHexDigit(value[i + 2])) {
            const auto high = HexValue(value[i + 1]);
            const auto low = HexValue(value[i + 2]);
            decoded.push_back(static_cast<char>((high << 4) | low));
            i += 2;
        } else if (c == '+') {
            decoded.push_back(' ');
        } else {
            decoded.push_back(c);
        }
    }
    return decoded;
}

std::optional<std::vector<uint8_t>> DecodeBase64(const std::string& input) {
    static const int8_t kLookup[256] = {
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,-1,63,
        52,53,54,55,56,57,58,59,60,61,-1,-1,-1,-2,-1,-1,
        -1,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,
        15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,
        -1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,
        41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1
    };

    std::vector<uint8_t> output;
    output.reserve((input.size() * 3) / 4);

    int32_t buffer = 0;
    int bits_collected = 0;
    for (unsigned char c : input) {
        const int8_t code = kLookup[c];
        if (code == -1) {
            if (std::isspace(c) != 0) {
                continue;
            }
            return std::nullopt;
        }
        if (code == -2) {
            break;
        }
        buffer = (buffer << 6) | code;
        bits_collected += 6;
        if (bits_collected >= 8) {
            bits_collected -= 8;
            output.push_back(static_cast<uint8_t>((buffer >> bits_collected) & 0xFF));
        }
    }
    return output;
}

std::optional<std::vector<uint8_t>> DecodeDataURL(const std::string& href) {
    if (href.rfind("data:", 0) != 0) {
        return std::nullopt;
    }

    const auto comma = href.find(',');
    if (comma == std::string::npos || comma <= 5) {
        return std::nullopt;
    }

    const std::string metadata = Lower(href.substr(5, comma - 5));
    const std::string payload = href.substr(comma + 1);
    const bool is_base64 = metadata.find(";base64") != std::string::npos;
    if (is_base64) {
        return DecodeBase64(payload);
    }

    const auto decoded = PercentDecode(payload);
    return std::vector<uint8_t>(decoded.begin(), decoded.end());
}

CGImageRef CreateImageFromData(const std::vector<uint8_t>& bytes) {
    if (bytes.empty()) {
        return nullptr;
    }

    CFDataRef data = CFDataCreate(kCFAllocatorDefault, bytes.data(), static_cast<CFIndex>(bytes.size()));
    if (data == nullptr) {
        return nullptr;
    }

    CGImageSourceRef source = CGImageSourceCreateWithData(data, nullptr);
    CFRelease(data);
    if (source == nullptr) {
        return nullptr;
    }

    CGImageRef image = CGImageSourceCreateImageAtIndex(source, 0, nullptr);
    CFRelease(source);
    return image;
}

CGImageRef LoadImageFromHref(const std::string& raw_href) {
    const auto href = Trim(raw_href);
    if (href.empty()) {
        return nullptr;
    }

    if (href.rfind("data:", 0) == 0) {
        const auto bytes = DecodeDataURL(href);
        if (!bytes.has_value()) {
            return nullptr;
        }
        return CreateImageFromData(*bytes);
    }

    if (href.rfind("file://", 0) == 0) {
        CFURLRef url = CFURLCreateWithBytes(kCFAllocatorDefault,
                                            reinterpret_cast<const UInt8*>(href.data()),
                                            static_cast<CFIndex>(href.size()),
                                            kCFStringEncodingUTF8,
                                            nullptr);
        if (url == nullptr) {
            return nullptr;
        }
        CGImageSourceRef source = CGImageSourceCreateWithURL(url, nullptr);
        CFRelease(url);
        if (source == nullptr) {
            return nullptr;
        }
        CGImageRef image = CGImageSourceCreateImageAtIndex(source, 0, nullptr);
        CFRelease(source);
        return image;
    }

    if (href.rfind("http://", 0) == 0 || href.rfind("https://", 0) == 0) {
        return nullptr;
    }

    CFURLRef url = CFURLCreateFromFileSystemRepresentation(kCFAllocatorDefault,
                                                            reinterpret_cast<const UInt8*>(href.data()),
                                                            static_cast<CFIndex>(href.size()),
                                                            false);
    if (url == nullptr) {
        return nullptr;
    }
    CGImageSourceRef source = CGImageSourceCreateWithURL(url, nullptr);
    CFRelease(url);
    if (source == nullptr) {
        return nullptr;
    }
    CGImageRef image = CGImageSourceCreateImageAtIndex(source, 0, nullptr);
    CFRelease(source);
    return image;
}

std::optional<std::string> ExtractHrefID(const XmlNode& node) {
    const auto href_it = node.attributes.find("href");
    const auto xlink_href_it = node.attributes.find("xlink:href");
    std::string value;
    if (href_it != node.attributes.end()) {
        value = Trim(href_it->second);
    } else if (xlink_href_it != node.attributes.end()) {
        value = Trim(xlink_href_it->second);
    }
    if (value.empty() || value.front() != '#' || value.size() < 2) {
        return std::nullopt;
    }
    return value.substr(1);
}

std::optional<std::string> ExtractHrefValue(const XmlNode& node) {
    const auto href_it = node.attributes.find("href");
    const auto xlink_href_it = node.attributes.find("xlink:href");
    if (href_it != node.attributes.end()) {
        const auto value = Trim(href_it->second);
        if (!value.empty()) {
            return value;
        }
    }
    if (xlink_href_it != node.attributes.end()) {
        const auto value = Trim(xlink_href_it->second);
        if (!value.empty()) {
            return value;
        }
    }
    return std::nullopt;
}

std::optional<std::string> ResolveLocalFilePathFromHref(const std::string& raw_href) {
    const auto href = Trim(raw_href);
    if (href.empty()) {
        return std::nullopt;
    }
    if (href.rfind("http://", 0) == 0 || href.rfind("https://", 0) == 0) {
        return std::nullopt;
    }
    if (href.rfind("file://", 0) == 0) {
        CFURLRef url = CFURLCreateWithBytes(kCFAllocatorDefault,
                                            reinterpret_cast<const UInt8*>(href.data()),
                                            static_cast<CFIndex>(href.size()),
                                            kCFStringEncodingUTF8,
                                            nullptr);
        if (url == nullptr) {
            return std::nullopt;
        }
        CFStringRef path = CFURLCopyFileSystemPath(url, kCFURLPOSIXPathStyle);
        CFRelease(url);
        if (path == nullptr) {
            return std::nullopt;
        }
        char buffer[4096];
        const bool ok = CFStringGetCString(path, buffer, sizeof(buffer), kCFStringEncodingUTF8);
        CFRelease(path);
        if (!ok) {
            return std::nullopt;
        }
        return std::string(buffer);
    }
    return href;
}

std::optional<std::vector<uint8_t>> LoadBytesFromHref(const std::string& raw_href) {
    const auto href = Trim(raw_href);
    if (href.empty()) {
        return std::nullopt;
    }
    if (href.rfind("data:", 0) == 0) {
        return DecodeDataURL(href);
    }
    const auto path = ResolveLocalFilePathFromHref(href);
    if (!path.has_value()) {
        return std::nullopt;
    }

    std::ifstream input(*path, std::ios::binary);
    if (!input.is_open()) {
        return std::nullopt;
    }
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    if (bytes.empty()) {
        return std::nullopt;
    }
    return bytes;
}

void CollectColorProfiles(const XmlNode& node, ColorProfileMap& profiles) {
    if (Lower(node.name) == "color-profile") {
        const auto href = ExtractHrefValue(node);
        if (href.has_value()) {
            const auto add_key = [&](const std::string& raw_key) {
                const auto key = Trim(raw_key);
                if (key.empty()) {
                    return;
                }
                profiles[key] = *href;
                profiles[Lower(key)] = *href;
            };
            if (const auto id_it = node.attributes.find("id"); id_it != node.attributes.end()) {
                add_key(id_it->second);
            }
            if (const auto name_it = node.attributes.find("name"); name_it != node.attributes.end()) {
                add_key(name_it->second);
            }
        }
    }
    for (const auto& child : node.children) {
        CollectColorProfiles(child, profiles);
    }
}

std::optional<std::string> ExtractColorProfileReference(const std::string& raw_value) {
    const auto value = Trim(raw_value);
    if (value.empty()) {
        return std::nullopt;
    }
    const auto lower = Lower(value);
    if (lower == "auto" || lower == "srgb" || lower == "inherit") {
        return std::nullopt;
    }
    if (lower.rfind("url(", 0) == 0) {
        const auto close = value.find(')');
        if (close == std::string::npos || close <= 4) {
            return std::nullopt;
        }
        auto inside = Trim(value.substr(4, close - 4));
        if (!inside.empty() && inside.front() == '#') {
            inside.erase(inside.begin());
        }
        if (inside.size() >= 2 &&
            ((inside.front() == '\'' && inside.back() == '\'') ||
             (inside.front() == '"' && inside.back() == '"'))) {
            inside = inside.substr(1, inside.size() - 2);
        }
        inside = Trim(inside);
        if (inside.empty()) {
            return std::nullopt;
        }
        return inside;
    }
    return value;
}

CGColorSpaceRef CreateICCColorSpaceFromHref(const std::string& profile_href) {
    const auto bytes = LoadBytesFromHref(profile_href);
    if (!bytes.has_value() || bytes->empty()) {
        return nullptr;
    }

    CFDataRef data = CFDataCreate(kCFAllocatorDefault, bytes->data(), static_cast<CFIndex>(bytes->size()));
    if (data == nullptr) {
        return nullptr;
    }
    CGColorSpaceRef color_space = CGColorSpaceCreateWithICCData(data);
    CFRelease(data);
    return color_space;
}

CGImageRef ConvertImageFromAssignedICCProfile(CGImageRef image, CGColorSpaceRef source_color_space) {
    if (image == nullptr || source_color_space == nullptr) {
        return nullptr;
    }

    const size_t width = CGImageGetWidth(image);
    const size_t height = CGImageGetHeight(image);
    if (width == 0 || height == 0) {
        return nullptr;
    }

    CGColorSpaceRef rgba_color_space = CGColorSpaceCreateDeviceRGB();
    if (rgba_color_space == nullptr) {
        return nullptr;
    }

    CGContextRef extraction_context = CGBitmapContextCreate(nullptr,
                                                            width,
                                                            height,
                                                            8,
                                                            0,
                                                            rgba_color_space,
                                                            static_cast<CGBitmapInfo>(kCGImageAlphaPremultipliedLast));
    if (extraction_context == nullptr) {
        CGColorSpaceRelease(rgba_color_space);
        return nullptr;
    }
    CGContextDrawImage(extraction_context, CGRectMake(0.0, 0.0, width, height), image);

    const auto extraction_data = static_cast<const UInt8*>(CGBitmapContextGetData(extraction_context));
    const size_t bytes_per_row = CGBitmapContextGetBytesPerRow(extraction_context);
    const size_t total_size = bytes_per_row * height;
    if (extraction_data == nullptr || total_size == 0) {
        CGContextRelease(extraction_context);
        CGColorSpaceRelease(rgba_color_space);
        return nullptr;
    }

    CFDataRef source_data = CFDataCreate(kCFAllocatorDefault, extraction_data, static_cast<CFIndex>(total_size));
    CGContextRelease(extraction_context);
    if (source_data == nullptr) {
        CGColorSpaceRelease(rgba_color_space);
        return nullptr;
    }

    CGDataProviderRef source_provider = CGDataProviderCreateWithCFData(source_data);
    CFRelease(source_data);
    if (source_provider == nullptr) {
        CGColorSpaceRelease(rgba_color_space);
        return nullptr;
    }

    CGImageRef source_tagged = CGImageCreate(width,
                                             height,
                                             8,
                                             32,
                                             bytes_per_row,
                                             source_color_space,
                                             static_cast<CGBitmapInfo>(kCGImageAlphaPremultipliedLast),
                                             source_provider,
                                             nullptr,
                                             true,
                                             kCGRenderingIntentDefault);
    CGDataProviderRelease(source_provider);
    if (source_tagged == nullptr) {
        CGColorSpaceRelease(rgba_color_space);
        return nullptr;
    }

    CGColorSpaceRef destination_space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    if (destination_space == nullptr) {
        destination_space = CGColorSpaceCreateDeviceRGB();
    }
    if (destination_space == nullptr) {
        CGImageRelease(source_tagged);
        CGColorSpaceRelease(rgba_color_space);
        return nullptr;
    }

    CGContextRef conversion_context = CGBitmapContextCreate(nullptr,
                                                            width,
                                                            height,
                                                            8,
                                                            0,
                                                            destination_space,
                                                            static_cast<CGBitmapInfo>(kCGImageAlphaPremultipliedLast));
    if (conversion_context == nullptr) {
        CGColorSpaceRelease(destination_space);
        CGImageRelease(source_tagged);
        CGColorSpaceRelease(rgba_color_space);
        return nullptr;
    }
    CGContextDrawImage(conversion_context, CGRectMake(0.0, 0.0, width, height), source_tagged);
    CGImageRelease(source_tagged);

    CGImageRef converted = CGBitmapContextCreateImage(conversion_context);
    CGContextRelease(conversion_context);
    CGColorSpaceRelease(destination_space);
    CGColorSpaceRelease(rgba_color_space);
    return converted;
}

CGAffineTransform ParseTransformList(const std::string& raw) {
    CGAffineTransform transform = CGAffineTransformIdentity;
    size_t i = 0;

    while (i < raw.size()) {
        while (i < raw.size() && (std::isspace(static_cast<unsigned char>(raw[i])) || raw[i] == ',')) {
            ++i;
        }
        if (i >= raw.size()) {
            break;
        }

        const size_t name_start = i;
        while (i < raw.size() && std::isalpha(static_cast<unsigned char>(raw[i]))) {
            ++i;
        }
        if (i <= name_start) {
            ++i;
            continue;
        }

        const std::string name = Lower(raw.substr(name_start, i - name_start));
        while (i < raw.size() && std::isspace(static_cast<unsigned char>(raw[i]))) {
            ++i;
        }
        if (i >= raw.size() || raw[i] != '(') {
            continue;
        }
        ++i;

        std::vector<double> params;
        while (i < raw.size() && raw[i] != ')') {
            while (i < raw.size() && (std::isspace(static_cast<unsigned char>(raw[i])) || raw[i] == ',')) {
                ++i;
            }
            if (i >= raw.size() || raw[i] == ')') {
                break;
            }

            const char* start = raw.c_str() + i;
            char* end_ptr = nullptr;
            const double value = std::strtod(start, &end_ptr);
            if (end_ptr == start) {
                ++i;
                continue;
            }
            params.push_back(value);
            i = static_cast<size_t>(end_ptr - raw.c_str());
        }

        if (i < raw.size() && raw[i] == ')') {
            ++i;
        }

        CGAffineTransform current = CGAffineTransformIdentity;
        if (name == "matrix" && params.size() >= 6) {
            current = CGAffineTransformMake(static_cast<CGFloat>(params[0]),
                                            static_cast<CGFloat>(params[1]),
                                            static_cast<CGFloat>(params[2]),
                                            static_cast<CGFloat>(params[3]),
                                            static_cast<CGFloat>(params[4]),
                                            static_cast<CGFloat>(params[5]));
        } else if (name == "translate" && !params.empty()) {
            const double tx = params[0];
            const double ty = params.size() > 1 ? params[1] : 0.0;
            current = CGAffineTransformMakeTranslation(static_cast<CGFloat>(tx), static_cast<CGFloat>(ty));
        } else if (name == "scale" && !params.empty()) {
            const double sx = params[0];
            const double sy = params.size() > 1 ? params[1] : sx;
            current = CGAffineTransformMakeScale(static_cast<CGFloat>(sx), static_cast<CGFloat>(sy));
        } else if (name == "rotate" && !params.empty()) {
            const double angle_rad = params[0] * M_PI / 180.0;
            if (params.size() > 2) {
                const double cx = params[1];
                const double cy = params[2];
                current = CGAffineTransformIdentity;
                current = CGAffineTransformTranslate(current, static_cast<CGFloat>(cx), static_cast<CGFloat>(cy));
                current = CGAffineTransformRotate(current, static_cast<CGFloat>(angle_rad));
                current = CGAffineTransformTranslate(current, static_cast<CGFloat>(-cx), static_cast<CGFloat>(-cy));
            } else {
                current = CGAffineTransformMakeRotation(static_cast<CGFloat>(angle_rad));
            }
        } else if (name == "skewx" && !params.empty()) {
            const CGFloat tangent = static_cast<CGFloat>(std::tan(params[0] * M_PI / 180.0));
            current = CGAffineTransformMake(1.0f, 0.0f, tangent, 1.0f, 0.0f, 0.0f);
        } else if (name == "skewy" && !params.empty()) {
            const CGFloat tangent = static_cast<CGFloat>(std::tan(params[0] * M_PI / 180.0));
            current = CGAffineTransformMake(1.0f, tangent, 0.0f, 1.0f, 0.0f, 0.0f);
        }

        // SVG applies listed transforms from left-to-right.
        transform = CGAffineTransformConcat(current, transform);
    }

    return transform;
}

std::optional<std::string> ExtractPaintURLId(const std::string& paint) {
    const std::string trimmed = Trim(paint);
    const std::string lower = Lower(trimmed);
    if (lower.rfind("url(", 0) != 0) {
        return std::nullopt;
    }
    const auto close = trimmed.find(')');
    if (close == std::string::npos || close <= 4) {
        return std::nullopt;
    }

    std::string inside = Trim(trimmed.substr(4, close - 4));
    if (inside.size() >= 2 &&
        ((inside.front() == '\'' && inside.back() == '\'') || (inside.front() == '"' && inside.back() == '"'))) {
        inside = inside.substr(1, inside.size() - 2);
    }

    if (!inside.empty() && inside.front() == '#') {
        inside.erase(inside.begin());
    }
    if (inside.empty()) {
        return std::nullopt;
    }
    return inside;
}

void CollectGradients(const XmlNode& node, GradientMap& gradients) {
    if (node.name == "linearGradient" || node.name == "radialGradient") {
        GradientDefinition gradient;
        gradient.type = node.name == "linearGradient" ? GradientType::kLinear : GradientType::kRadial;

        const auto gradient_style_it = node.attributes.find("style");
        const auto gradient_inline_style = gradient_style_it != node.attributes.end()
            ? ParseInlineStyle(gradient_style_it->second)
            : std::map<std::string, std::string>{};

        Color gradient_color = StyleResolver::ParseColor("black");
        if (const auto color_value = ReadAttrOrStyle(node, gradient_inline_style, "color"); color_value.has_value()) {
            const auto parsed = StyleResolver::ParseColor(*color_value);
            if (parsed.is_valid && !parsed.is_none) {
                gradient_color = parsed;
            }
        }

        const auto id_it = node.attributes.find("id");
        if (id_it != node.attributes.end()) {
            gradient.id = id_it->second;
        }

        const auto units_it = node.attributes.find("gradientUnits");
        if (units_it != node.attributes.end()) {
            gradient.user_space_units = Lower(Trim(units_it->second)) == "userspaceonuse";
        }

        const auto transform_it = node.attributes.find("gradientTransform");
        if (transform_it != node.attributes.end()) {
            gradient.transform = ParseTransformList(transform_it->second);
        }

        if (gradient.type == GradientType::kLinear) {
            const auto x1_it = node.attributes.find("x1");
            const auto y1_it = node.attributes.find("y1");
            const auto x2_it = node.attributes.find("x2");
            const auto y2_it = node.attributes.find("y2");

            gradient.x1 = x1_it != node.attributes.end() ? ParseCoordinate(x1_it->second, 0.0) : 0.0;
            gradient.y1 = y1_it != node.attributes.end() ? ParseCoordinate(y1_it->second, 0.0) : 0.0;
            gradient.x2 = x2_it != node.attributes.end() ? ParseCoordinate(x2_it->second, 1.0) : 1.0;
            gradient.y2 = y2_it != node.attributes.end() ? ParseCoordinate(y2_it->second, 0.0) : 0.0;
        } else {
            const auto cx_it = node.attributes.find("cx");
            const auto cy_it = node.attributes.find("cy");
            const auto r_it = node.attributes.find("r");
            const auto fx_it = node.attributes.find("fx");
            const auto fy_it = node.attributes.find("fy");

            gradient.cx = cx_it != node.attributes.end() ? ParseCoordinate(cx_it->second, 0.5) : 0.5;
            gradient.cy = cy_it != node.attributes.end() ? ParseCoordinate(cy_it->second, 0.5) : 0.5;
            gradient.r = r_it != node.attributes.end() ? ParseCoordinate(r_it->second, 0.5) : 0.5;
            gradient.fx = fx_it != node.attributes.end() ? ParseCoordinate(fx_it->second, gradient.cx) : gradient.cx;
            gradient.fy = fy_it != node.attributes.end() ? ParseCoordinate(fy_it->second, gradient.cy) : gradient.cy;
        }

        for (const auto& stop_node : node.children) {
            if (stop_node.name != "stop") {
                continue;
            }

            const auto style_it = stop_node.attributes.find("style");
            const auto inline_style = style_it != stop_node.attributes.end() ? ParseInlineStyle(style_it->second) : std::map<std::string, std::string>{};

            GradientStop stop;
            stop.offset = 0.0;
            stop.color = StyleResolver::ParseColor("black");
            stop.opacity = 1.0;

            Color stop_current_color = gradient_color;
            if (const auto stop_color_prop = ReadAttrOrStyle(stop_node, inline_style, "color"); stop_color_prop.has_value()) {
                const auto parsed = StyleResolver::ParseColor(*stop_color_prop);
                if (parsed.is_valid && !parsed.is_none) {
                    stop_current_color = parsed;
                }
            }

            if (const auto offset = ReadAttrOrStyle(stop_node, inline_style, "offset"); offset.has_value()) {
                stop.offset = ParseOffset(*offset);
            }

            if (const auto stop_color = ReadAttrOrStyle(stop_node, inline_style, "stop-color"); stop_color.has_value()) {
                const auto stop_color_lower = Lower(Trim(*stop_color));
                if (stop_color_lower == "currentcolor") {
                    stop.color = stop_current_color;
                } else {
                    const auto parsed_color = StyleResolver::ParseColor(*stop_color);
                    if (parsed_color.is_valid) {
                        stop.color = parsed_color;
                    }
                }
            }

            if (const auto stop_opacity = ReadAttrOrStyle(stop_node, inline_style, "stop-opacity"); stop_opacity.has_value()) {
                stop.opacity = std::clamp(ParseDouble(*stop_opacity, 1.0), 0.0, 1.0);
            }

            gradient.stops.push_back(stop);
        }

        if (gradient.stops.empty()) {
            GradientStop start;
            start.offset = 0.0;
            start.color = StyleResolver::ParseColor("black");
            gradient.stops.push_back(start);

            GradientStop end;
            end.offset = 1.0;
            end.color = StyleResolver::ParseColor("white");
            gradient.stops.push_back(end);
        }

        std::sort(gradient.stops.begin(), gradient.stops.end(), [](const GradientStop& lhs, const GradientStop& rhs) {
            return lhs.offset < rhs.offset;
        });

        if (!gradient.id.empty()) {
            gradients[gradient.id] = gradient;
        }
    }

    for (const auto& child : node.children) {
        CollectGradients(child, gradients);
    }
}

void CollectPatterns(const XmlNode& node, PatternMap& patterns) {
    if (Lower(node.name) == "pattern") {
        PatternDefinition pattern;
        pattern.node = &node;

        if (const auto id_it = node.attributes.find("id"); id_it != node.attributes.end()) {
            pattern.id = Trim(id_it->second);
        }

        if (const auto units_it = node.attributes.find("patternUnits"); units_it != node.attributes.end()) {
            pattern.pattern_units_user_space = Lower(Trim(units_it->second)) == "userspaceonuse";
        }
        if (const auto content_units_it = node.attributes.find("patternContentUnits"); content_units_it != node.attributes.end()) {
            pattern.content_units_user_space = Lower(Trim(content_units_it->second)) == "userspaceonuse";
        }
        if (const auto transform_it = node.attributes.find("patternTransform"); transform_it != node.attributes.end()) {
            pattern.transform = ParseTransformList(transform_it->second);
        }

        if (const auto x_it = node.attributes.find("x"); x_it != node.attributes.end()) {
            pattern.x = ParseCoordinate(x_it->second, 0.0);
        }
        if (const auto y_it = node.attributes.find("y"); y_it != node.attributes.end()) {
            pattern.y = ParseCoordinate(y_it->second, 0.0);
        }
        if (const auto width_it = node.attributes.find("width"); width_it != node.attributes.end()) {
            pattern.width = ParseCoordinate(width_it->second, 0.0);
        }
        if (const auto height_it = node.attributes.find("height"); height_it != node.attributes.end()) {
            pattern.height = ParseCoordinate(height_it->second, 0.0);
        }

        if (!pattern.id.empty()) {
            patterns[pattern.id] = pattern;
        }
    }

    for (const auto& child : node.children) {
        CollectPatterns(child, patterns);
    }
}

void ApplyColor(CGContextRef context, const Color& color, float opacity, bool stroke) {
    if (!color.is_valid || color.is_none) {
        return;
    }
    const CGFloat alpha = std::clamp(color.a * opacity, 0.0f, 1.0f);
    if (stroke) {
        CGContextSetRGBStrokeColor(context, color.r, color.g, color.b, alpha);
    } else {
        CGContextSetRGBFillColor(context, color.r, color.g, color.b, alpha);
    }
}

CGLineJoin LineJoinFromStyle(const std::string& join_style) {
    const auto lower = Lower(join_style);
    if (lower == "round") {
        return kCGLineJoinRound;
    }
    if (lower == "bevel") {
        return kCGLineJoinBevel;
    }
    return kCGLineJoinMiter;
}

CGLineCap LineCapFromStyle(const std::string& cap_style) {
    const auto lower = Lower(cap_style);
    if (lower == "round") {
        return kCGLineCapRound;
    }
    if (lower == "square") {
        return kCGLineCapSquare;
    }
    return kCGLineCapButt;
}

void PaintNode(const XmlNode& node,
               const StyleResolver& style_resolver,
               const GeometryEngine& geometry_engine,
               const ResolvedStyle* parent_style,
               CGContextRef context,
               const GradientMap& gradients,
               const PatternMap& patterns,
               const NodeIdMap& id_map,
               const ColorProfileMap& color_profiles,
               std::set<std::string>& active_use_ids,
               std::set<std::string>& active_pattern_ids,
               const RenderOptions& options,
               RenderError& error,
               bool apply_filters = true,
               bool suppress_current_opacity = false);

std::string LocalName(const std::string& name);

struct PixelSurface {
    size_t width = 0;
    size_t height = 0;
    std::vector<uint8_t> rgba;
};

struct PixelBounds {
    size_t min_x = 0;
    size_t min_y = 0;
    size_t max_x = 0;
    size_t max_y = 0;
};

std::optional<std::string> ResolveFilterID(const XmlNode& node, const std::map<std::string, std::string>& inline_style) {
    const auto filter_value = ReadAttrOrStyle(node, inline_style, "filter");
    if (!filter_value.has_value()) {
        return std::nullopt;
    }
    return ExtractPaintURLId(*filter_value);
}

std::optional<PixelSurface> RenderNodeToSurface(const XmlNode& node,
                                                const StyleResolver& style_resolver,
                                                const GeometryEngine& geometry_engine,
                                                const ResolvedStyle* parent_style,
                                                const GradientMap& gradients,
                                                const PatternMap& patterns,
                                                const NodeIdMap& id_map,
                                                const ColorProfileMap& color_profiles,
                                                const RenderOptions& options,
                                                RenderError& error) {
    const size_t width = static_cast<size_t>(std::max(1.0, std::ceil(std::max(geometry_engine.viewport_width(), 1.0))));
    const size_t height = static_cast<size_t>(std::max(1.0, std::ceil(std::max(geometry_engine.viewport_height(), 1.0))));
    const size_t bytes_per_row = width * 4;

    CGColorSpaceRef color_space = CGColorSpaceCreateDeviceRGB();
    CGContextRef bitmap = CGBitmapContextCreate(nullptr,
                                                width,
                                                height,
                                                8,
                                                bytes_per_row,
                                                color_space,
                                                static_cast<CGBitmapInfo>(kCGImageAlphaPremultipliedLast));
    CGColorSpaceRelease(color_space);
    if (bitmap == nullptr) {
        return std::nullopt;
    }

    CGContextTranslateCTM(bitmap, 0.0, static_cast<CGFloat>(height));
    CGContextScaleCTM(bitmap, 1.0, -1.0);

    std::set<std::string> local_active_use_ids;
    std::set<std::string> local_active_pattern_ids;
    RenderError local_error;
    PaintNode(node,
              style_resolver,
              geometry_engine,
              parent_style,
              bitmap,
              gradients,
              patterns,
              id_map,
              color_profiles,
              local_active_use_ids,
              local_active_pattern_ids,
              options,
              local_error,
              false,
              true);

    if (local_error.code != RenderErrorCode::kNone) {
        CGContextRelease(bitmap);
        error = local_error;
        return std::nullopt;
    }

    const auto* source_data = static_cast<const uint8_t*>(CGBitmapContextGetData(bitmap));
    const size_t source_stride = CGBitmapContextGetBytesPerRow(bitmap);
    if (source_data == nullptr || source_stride == 0) {
        CGContextRelease(bitmap);
        return std::nullopt;
    }

    PixelSurface surface;
    surface.width = width;
    surface.height = height;
    surface.rgba.resize(bytes_per_row * height, 0);
    for (size_t row = 0; row < height; ++row) {
        std::memcpy(surface.rgba.data() + row * bytes_per_row,
                    source_data + row * source_stride,
                    bytes_per_row);
    }
    CGContextRelease(bitmap);
    return surface;
}

PixelBounds ComputeNonTransparentBounds(const PixelSurface& surface) {
    PixelBounds bounds{
        surface.width,
        surface.height,
        0,
        0
    };
    bool found = false;

    for (size_t y = 0; y < surface.height; ++y) {
        for (size_t x = 0; x < surface.width; ++x) {
            const size_t base = ((y * surface.width) + x) * 4;
            if (surface.rgba[base + 3] == 0) {
                continue;
            }
            if (!found) {
                bounds.min_x = x;
                bounds.max_x = x;
                bounds.min_y = y;
                bounds.max_y = y;
                found = true;
                continue;
            }
            bounds.min_x = std::min(bounds.min_x, x);
            bounds.max_x = std::max(bounds.max_x, x);
            bounds.min_y = std::min(bounds.min_y, y);
            bounds.max_y = std::max(bounds.max_y, y);
        }
    }

    if (!found) {
        bounds.min_x = 0;
        bounds.min_y = 0;
        bounds.max_x = surface.width > 0 ? surface.width - 1 : 0;
        bounds.max_y = surface.height > 0 ? surface.height - 1 : 0;
    }

    return bounds;
}

PixelSurface MakeFloodSurface(size_t width, size_t height, const XmlNode& primitive, const PixelBounds& bounds) {
    PixelSurface surface;
    surface.width = width;
    surface.height = height;
    surface.rgba.resize(width * height * 4, 0);
    if (width == 0 || height == 0) {
        return surface;
    }

    const auto style_it = primitive.attributes.find("style");
    const auto inline_style = style_it != primitive.attributes.end()
        ? ParseInlineStyle(style_it->second)
        : std::map<std::string, std::string>{};

    const auto flood_color_text = ReadAttrOrStyle(primitive, inline_style, "flood-color").value_or("black");
    const auto flood_opacity_text = ReadAttrOrStyle(primitive, inline_style, "flood-opacity").value_or("1");
    auto flood_color = StyleResolver::ParseColor(flood_color_text);
    if (!flood_color.is_valid || flood_color.is_none) {
        flood_color = StyleResolver::ParseColor("black");
    }

    const double opacity = std::clamp(ParseDouble(flood_opacity_text, 1.0), 0.0, 1.0);
    const double alpha = std::clamp(static_cast<double>(flood_color.a) * opacity, 0.0, 1.0);
    const uint8_t a = static_cast<uint8_t>(std::lround(alpha * 255.0));
    const uint8_t r = static_cast<uint8_t>(std::lround(std::clamp(static_cast<double>(flood_color.r) * alpha, 0.0, 1.0) * 255.0));
    const uint8_t g = static_cast<uint8_t>(std::lround(std::clamp(static_cast<double>(flood_color.g) * alpha, 0.0, 1.0) * 255.0));
    const uint8_t b = static_cast<uint8_t>(std::lround(std::clamp(static_cast<double>(flood_color.b) * alpha, 0.0, 1.0) * 255.0));

    const size_t max_x = std::min(bounds.max_x, width > 0 ? width - 1 : 0);
    const size_t max_y = std::min(bounds.max_y, height > 0 ? height - 1 : 0);
    const size_t min_x = std::min(bounds.min_x, max_x);
    const size_t min_y = std::min(bounds.min_y, max_y);

    for (size_t y = min_y; y <= max_y; ++y) {
        for (size_t x = min_x; x <= max_x; ++x) {
            const size_t base = ((y * width) + x) * 4;
            surface.rgba[base + 0] = r;
            surface.rgba[base + 1] = g;
            surface.rgba[base + 2] = b;
            surface.rgba[base + 3] = a;
        }
    }
    return surface;
}

double BlendChannel(double cb, double cs, const std::string& mode) {
    if (mode == "multiply") {
        return cb * cs;
    }
    if (mode == "screen") {
        return cb + cs - (cb * cs);
    }
    if (mode == "darken") {
        return std::min(cb, cs);
    }
    if (mode == "lighten") {
        return std::max(cb, cs);
    }
    return cs;
}

double SRGBToLinear(double value) {
    value = std::clamp(value, 0.0, 1.0);
    if (value <= 0.04045) {
        return value / 12.92;
    }
    return std::pow((value + 0.055) / 1.055, 2.4);
}

double LinearToSRGB(double value) {
    value = std::clamp(value, 0.0, 1.0);
    if (value <= 0.0031308) {
        return value * 12.92;
    }
    return (1.055 * std::pow(value, 1.0 / 2.4)) - 0.055;
}

PixelSurface BlendSurfaces(const PixelSurface& in_surface, const PixelSurface& in2_surface, const std::string& mode) {
    PixelSurface out;
    out.width = in_surface.width;
    out.height = in_surface.height;
    out.rgba.resize(out.width * out.height * 4, 0);

    for (size_t i = 0; i < out.width * out.height; ++i) {
        const size_t base = i * 4;
        const double s_a = static_cast<double>(in_surface.rgba[base + 3]) / 255.0;
        const double b_a = static_cast<double>(in2_surface.rgba[base + 3]) / 255.0;

        const auto to_straight = [](uint8_t premul, double alpha) -> double {
            if (alpha <= 0.0) {
                return 0.0;
            }
            return std::clamp((static_cast<double>(premul) / 255.0) / alpha, 0.0, 1.0);
        };

        const double s_r = SRGBToLinear(to_straight(in_surface.rgba[base + 0], s_a));
        const double s_g = SRGBToLinear(to_straight(in_surface.rgba[base + 1], s_a));
        const double s_b = SRGBToLinear(to_straight(in_surface.rgba[base + 2], s_a));
        const double b_r = SRGBToLinear(to_straight(in2_surface.rgba[base + 0], b_a));
        const double b_g = SRGBToLinear(to_straight(in2_surface.rgba[base + 1], b_a));
        const double b_b = SRGBToLinear(to_straight(in2_surface.rgba[base + 2], b_a));

        const double out_a = std::clamp(s_a + b_a - (s_a * b_a), 0.0, 1.0);
        const double out_r_premul_linear = std::clamp(s_a * (1.0 - b_a) * s_r +
                                                      b_a * (1.0 - s_a) * b_r +
                                                      s_a * b_a * BlendChannel(b_r, s_r, mode), 0.0, 1.0);
        const double out_g_premul_linear = std::clamp(s_a * (1.0 - b_a) * s_g +
                                                      b_a * (1.0 - s_a) * b_g +
                                                      s_a * b_a * BlendChannel(b_g, s_g, mode), 0.0, 1.0);
        const double out_b_premul_linear = std::clamp(s_a * (1.0 - b_a) * s_b +
                                                      b_a * (1.0 - s_a) * b_b +
                                                      s_a * b_a * BlendChannel(b_b, s_b, mode), 0.0, 1.0);

        auto linear_premul_to_srgb_premul = [out_a](double premul_linear) -> double {
            if (out_a <= 0.0) {
                return 0.0;
            }
            const double straight_linear = std::clamp(premul_linear / out_a, 0.0, 1.0);
            const double straight_srgb = LinearToSRGB(straight_linear);
            return std::clamp(straight_srgb * out_a, 0.0, 1.0);
        };

        const double out_r_premul = linear_premul_to_srgb_premul(out_r_premul_linear);
        const double out_g_premul = linear_premul_to_srgb_premul(out_g_premul_linear);
        const double out_b_premul = linear_premul_to_srgb_premul(out_b_premul_linear);

        out.rgba[base + 0] = static_cast<uint8_t>(std::lround(out_r_premul * 255.0));
        out.rgba[base + 1] = static_cast<uint8_t>(std::lround(out_g_premul * 255.0));
        out.rgba[base + 2] = static_cast<uint8_t>(std::lround(out_b_premul * 255.0));
        out.rgba[base + 3] = static_cast<uint8_t>(std::lround(out_a * 255.0));
    }

    return out;
}

std::vector<double> ParseNumberList(const std::string& text) {
    std::vector<double> values;
    size_t index = 0;
    while (index < text.size()) {
        while (index < text.size() &&
               (std::isspace(static_cast<unsigned char>(text[index])) || text[index] == ',')) {
            ++index;
        }
        if (index >= text.size()) {
            break;
        }

        char* end_ptr = nullptr;
        const double value = std::strtod(text.c_str() + index, &end_ptr);
        if (end_ptr == text.c_str() + index) {
            ++index;
            continue;
        }
        values.push_back(value);
        index = static_cast<size_t>(end_ptr - text.c_str());
    }
    return values;
}

struct RGBAColor {
    double r = 0.0;
    double g = 0.0;
    double b = 0.0;
    double a = 0.0;
};

RGBAColor ReadPixelStraightLinear(const PixelSurface& surface, size_t base) {
    const double alpha = static_cast<double>(surface.rgba[base + 3]) / 255.0;
    auto to_straight = [alpha](uint8_t premul) -> double {
        if (alpha <= 0.0) {
            return 0.0;
        }
        return std::clamp((static_cast<double>(premul) / 255.0) / alpha, 0.0, 1.0);
    };

    RGBAColor color;
    color.a = alpha;
    color.r = SRGBToLinear(to_straight(surface.rgba[base + 0]));
    color.g = SRGBToLinear(to_straight(surface.rgba[base + 1]));
    color.b = SRGBToLinear(to_straight(surface.rgba[base + 2]));
    return color;
}

void WritePixelFromStraightLinear(PixelSurface& surface, size_t base, const RGBAColor& color) {
    const double alpha = std::clamp(color.a, 0.0, 1.0);
    const double r = std::clamp(color.r, 0.0, 1.0);
    const double g = std::clamp(color.g, 0.0, 1.0);
    const double b = std::clamp(color.b, 0.0, 1.0);

    const double r_srgb = LinearToSRGB(r);
    const double g_srgb = LinearToSRGB(g);
    const double b_srgb = LinearToSRGB(b);

    surface.rgba[base + 0] = static_cast<uint8_t>(std::lround(std::clamp(r_srgb * alpha, 0.0, 1.0) * 255.0));
    surface.rgba[base + 1] = static_cast<uint8_t>(std::lround(std::clamp(g_srgb * alpha, 0.0, 1.0) * 255.0));
    surface.rgba[base + 2] = static_cast<uint8_t>(std::lround(std::clamp(b_srgb * alpha, 0.0, 1.0) * 255.0));
    surface.rgba[base + 3] = static_cast<uint8_t>(std::lround(alpha * 255.0));
}

std::array<double, 20> IdentityColorMatrix() {
    return {
        1.0, 0.0, 0.0, 0.0, 0.0,
        0.0, 1.0, 0.0, 0.0, 0.0,
        0.0, 0.0, 1.0, 0.0, 0.0,
        0.0, 0.0, 0.0, 1.0, 0.0
    };
}

std::array<double, 20> MatrixForSaturate(double saturation) {
    const double s = saturation;
    return {
        0.213 + 0.787 * s, 0.715 - 0.715 * s, 0.072 - 0.072 * s, 0.0, 0.0,
        0.213 - 0.213 * s, 0.715 + 0.285 * s, 0.072 - 0.072 * s, 0.0, 0.0,
        0.213 - 0.213 * s, 0.715 - 0.715 * s, 0.072 + 0.928 * s, 0.0, 0.0,
        0.0,               0.0,               0.0,               1.0, 0.0
    };
}

std::array<double, 20> MatrixForHueRotate(double degrees) {
    const double radians = degrees * M_PI / 180.0;
    const double c = std::cos(radians);
    const double s = std::sin(radians);
    return {
        0.213 + c * 0.787 - s * 0.213, 0.715 - c * 0.715 - s * 0.715, 0.072 - c * 0.072 + s * 0.928, 0.0, 0.0,
        0.213 - c * 0.213 + s * 0.143, 0.715 + c * 0.285 + s * 0.140, 0.072 - c * 0.072 - s * 0.283, 0.0, 0.0,
        0.213 - c * 0.213 - s * 0.787, 0.715 - c * 0.715 + s * 0.715, 0.072 + c * 0.928 + s * 0.072, 0.0, 0.0,
        0.0,                            0.0,                            0.0,                            1.0, 0.0
    };
}

std::array<double, 20> MatrixForLuminanceToAlpha() {
    return {
        0.0,    0.0,    0.0,    0.0, 0.0,
        0.0,    0.0,    0.0,    0.0, 0.0,
        0.0,    0.0,    0.0,    0.0, 0.0,
        0.2126, 0.7152, 0.0722, 0.0, 0.0
    };
}

std::array<double, 20> ResolveColorMatrix(const XmlNode& primitive) {
    const std::string matrix_type = Lower(Trim(primitive.attributes.count("type") ? primitive.attributes.at("type") : "matrix"));
    const auto values = ParseNumberList(primitive.attributes.count("values") ? primitive.attributes.at("values") : "");

    if (matrix_type == "saturate") {
        return MatrixForSaturate(values.empty() ? 1.0 : values[0]);
    }
    if (matrix_type == "huerotate") {
        return MatrixForHueRotate(values.empty() ? 0.0 : values[0]);
    }
    if (matrix_type == "luminancetoalpha") {
        return MatrixForLuminanceToAlpha();
    }

    auto matrix = IdentityColorMatrix();
    if (values.size() >= 20) {
        for (size_t i = 0; i < 20; ++i) {
            matrix[i] = values[i];
        }
    }
    return matrix;
}

PixelSurface ApplyColorMatrix(const PixelSurface& input, const XmlNode& primitive) {
    PixelSurface output = input;
    const auto matrix = ResolveColorMatrix(primitive);

    for (size_t i = 0; i < input.width * input.height; ++i) {
        const size_t base = i * 4;
        const auto src = ReadPixelStraightLinear(input, base);
        RGBAColor dst;

        dst.r = matrix[0] * src.r + matrix[1] * src.g + matrix[2] * src.b + matrix[3] * src.a + matrix[4];
        dst.g = matrix[5] * src.r + matrix[6] * src.g + matrix[7] * src.b + matrix[8] * src.a + matrix[9];
        dst.b = matrix[10] * src.r + matrix[11] * src.g + matrix[12] * src.b + matrix[13] * src.a + matrix[14];
        dst.a = matrix[15] * src.r + matrix[16] * src.g + matrix[17] * src.b + matrix[18] * src.a + matrix[19];

        WritePixelFromStraightLinear(output, base, dst);
    }

    return output;
}

PixelSurface CompositeSurfaces(const PixelSurface& in_surface,
                              const PixelSurface& in2_surface,
                              const std::string& op,
                              double k1 = 0.0,
                              double k2 = 0.0,
                              double k3 = 0.0,
                              double k4 = 0.0) {
    PixelSurface out;
    out.width = in_surface.width;
    out.height = in_surface.height;
    out.rgba.resize(out.width * out.height * 4, 0);

    const std::string op_lower = Lower(op);
    const auto sample_premul = [](const PixelSurface& surface, size_t base, size_t channel) -> double {
        if (base + channel >= surface.rgba.size()) {
            return 0.0;
        }
        return static_cast<double>(surface.rgba[base + channel]) / 255.0;
    };
    const auto sample_alpha = [&](const PixelSurface& surface, size_t base) -> double {
        return sample_premul(surface, base, 3);
    };

    for (size_t i = 0; i < out.width * out.height; ++i) {
        const size_t base = i * 4;
        const double in_r = sample_premul(in_surface, base, 0);
        const double in_g = sample_premul(in_surface, base, 1);
        const double in_b = sample_premul(in_surface, base, 2);
        const double a_in = sample_alpha(in_surface, base);

        const double in2_r = sample_premul(in2_surface, base, 0);
        const double in2_g = sample_premul(in2_surface, base, 1);
        const double in2_b = sample_premul(in2_surface, base, 2);
        const double a_in2 = sample_alpha(in2_surface, base);

        auto write_premul = [&](double r, double g, double b, double a) {
            out.rgba[base + 0] = static_cast<uint8_t>(std::lround(std::clamp(r, 0.0, 1.0) * 255.0));
            out.rgba[base + 1] = static_cast<uint8_t>(std::lround(std::clamp(g, 0.0, 1.0) * 255.0));
            out.rgba[base + 2] = static_cast<uint8_t>(std::lround(std::clamp(b, 0.0, 1.0) * 255.0));
            out.rgba[base + 3] = static_cast<uint8_t>(std::lround(std::clamp(a, 0.0, 1.0) * 255.0));
        };

        if (op_lower == "in") {
            write_premul(in_r * a_in2, in_g * a_in2, in_b * a_in2, a_in * a_in2);
            continue;
        }

        if (op_lower == "out") {
            write_premul(in_r * (1.0 - a_in2),
                         in_g * (1.0 - a_in2),
                         in_b * (1.0 - a_in2),
                         a_in * (1.0 - a_in2));
            continue;
        }

        if (op_lower == "atop") {
            write_premul(in_r * a_in2 + in2_r * (1.0 - a_in),
                         in_g * a_in2 + in2_g * (1.0 - a_in),
                         in_b * a_in2 + in2_b * (1.0 - a_in),
                         a_in2);
            continue;
        }

        if (op_lower == "xor") {
            write_premul(in_r * (1.0 - a_in2) + in2_r * (1.0 - a_in),
                         in_g * (1.0 - a_in2) + in2_g * (1.0 - a_in),
                         in_b * (1.0 - a_in2) + in2_b * (1.0 - a_in),
                         a_in * (1.0 - a_in2) + a_in2 * (1.0 - a_in));
            continue;
        }

        if (op_lower == "arithmetic") {
            const auto arithmetic = [&](double c1, double c2) -> double {
                return std::clamp((k1 * c1 * c2) + (k2 * c1) + (k3 * c2) + k4, 0.0, 1.0);
            };

            // feComposite arithmetic is evaluated on premultiplied channels.
            // Keep empty input regions transparent (no k4 veil outside content).
            const bool has_coverage = (a_in > 0.0) || (a_in2 > 0.0);
            if (!has_coverage) {
                write_premul(0.0, 0.0, 0.0, 0.0);
                continue;
            }

            const double out_alpha = arithmetic(a_in, a_in2);
            const double out_r = std::min(arithmetic(in_r, in2_r), out_alpha);
            const double out_g = std::min(arithmetic(in_g, in2_g), out_alpha);
            const double out_b = std::min(arithmetic(in_b, in2_b), out_alpha);
            write_premul(out_r, out_g, out_b, out_alpha);
            continue;
        }

        // "over" default behavior for unhandled operators.
        write_premul(in_r + in2_r * (1.0 - a_in),
                     in_g + in2_g * (1.0 - a_in),
                     in_b + in2_b * (1.0 - a_in),
                     a_in + a_in2 * (1.0 - a_in));
    }

    return out;
}

CGImageRef CreateImageFromSurface(const PixelSurface& surface);

PixelSurface MakeTransparentSurface(size_t width, size_t height) {
    PixelSurface output;
    output.width = width;
    output.height = height;
    output.rgba.resize(width * height * 4, 0);
    return output;
}

bool IsEmptyBounds(const PixelBounds& bounds) {
    return bounds.max_x < bounds.min_x || bounds.max_y < bounds.min_y;
}

PixelBounds FullSurfaceBounds(const PixelSurface& surface) {
    if (surface.width == 0 || surface.height == 0) {
        return PixelBounds{0, 0, 0, 0};
    }
    return PixelBounds{0, 0, surface.width - 1, surface.height - 1};
}

PixelBounds ResolveUsableBounds(const PixelSurface& surface, const PixelBounds& candidate) {
    if (!IsEmptyBounds(candidate)) {
        return candidate;
    }
    return FullSurfaceBounds(surface);
}

PixelSurface CropSurface(const PixelSurface& surface, const PixelBounds& bounds) {
    if (surface.width == 0 || surface.height == 0 || IsEmptyBounds(bounds)) {
        return MakeTransparentSurface(0, 0);
    }

    const size_t crop_width = bounds.max_x - bounds.min_x + 1;
    const size_t crop_height = bounds.max_y - bounds.min_y + 1;
    PixelSurface cropped = MakeTransparentSurface(crop_width, crop_height);
    for (size_t row = 0; row < crop_height; ++row) {
        const size_t source_row = bounds.min_y + row;
        const size_t source_offset = ((source_row * surface.width) + bounds.min_x) * 4;
        const size_t target_offset = row * crop_width * 4;
        std::memcpy(cropped.rgba.data() + target_offset, surface.rgba.data() + source_offset, crop_width * 4);
    }
    return cropped;
}

void DrawCGImageToSurface(PixelSurface& surface, CGImageRef image, double x, double y, double width, double height) {
    if (image == nullptr || surface.width == 0 || surface.height == 0 || !(width > 0.0) || !(height > 0.0)) {
        return;
    }

    CGColorSpaceRef color_space = CGColorSpaceCreateDeviceRGB();
    CGContextRef bitmap = CGBitmapContextCreate(surface.rgba.data(),
                                                surface.width,
                                                surface.height,
                                                8,
                                                surface.width * 4,
                                                color_space,
                                                static_cast<CGBitmapInfo>(kCGImageAlphaPremultipliedLast));
    CGColorSpaceRelease(color_space);
    if (bitmap == nullptr) {
        return;
    }

    CGContextTranslateCTM(bitmap, 0.0, static_cast<CGFloat>(surface.height));
    CGContextScaleCTM(bitmap, 1.0, -1.0);

    CGContextSaveGState(bitmap);
    CGContextTranslateCTM(bitmap, static_cast<CGFloat>(x), static_cast<CGFloat>(y + height));
    CGContextScaleCTM(bitmap, 1.0, -1.0);
    CGContextDrawImage(bitmap,
                       CGRectMake(0.0, 0.0, static_cast<CGFloat>(width), static_cast<CGFloat>(height)),
                       image);
    CGContextRestoreGState(bitmap);
    CGContextRelease(bitmap);
}

PixelSurface RenderImageFilterPrimitive(const XmlNode& primitive,
                                        const PixelSurface& source_surface,
                                        const StyleResolver& style_resolver,
                                        const GeometryEngine& geometry_engine,
                                        const GradientMap& gradients,
                                        const PatternMap& patterns,
                                        const NodeIdMap& id_map,
                                        const ColorProfileMap& color_profiles,
                                        const RenderOptions& options,
                                        RenderError& error) {
    PixelSurface output = MakeTransparentSurface(source_surface.width, source_surface.height);
    const auto href = ExtractHrefValue(primitive);
    if (!href.has_value()) {
        return output;
    }

    if (!href->empty() && href->front() == '#') {
        const std::string id = href->substr(1);
        const auto it = id_map.find(id);
        if (it == id_map.end() || it->second == nullptr) {
            return output;
        }

        const PixelBounds source_bounds = ResolveUsableBounds(source_surface, ComputeNonTransparentBounds(source_surface));
        const double source_width = static_cast<double>(source_bounds.max_x - source_bounds.min_x + 1);
        const double source_height = static_cast<double>(source_bounds.max_y - source_bounds.min_y + 1);
        const GeometryEngine primitive_geometry_engine(std::max(source_width, 1.0),
                                                       std::max(source_height, 1.0));

        RenderError local_error;
        const auto rendered = RenderNodeToSurface(*it->second,
                                                  style_resolver,
                                                  primitive_geometry_engine,
                                                  nullptr,
                                                  gradients,
                                                  patterns,
                                                  id_map,
                                                  color_profiles,
                                                  options,
                                                  local_error);
        if (!rendered.has_value()) {
            return output;
        }

        CGImageRef image = CreateImageFromSurface(*rendered);
        if (image == nullptr) {
            return output;
        }
        const double viewport_width = static_cast<double>(std::max<size_t>(1, source_surface.width));
        const double viewport_height = static_cast<double>(std::max<size_t>(1, source_surface.height));
        const double default_x = static_cast<double>(source_bounds.min_x);
        const double default_y = static_cast<double>(source_bounds.min_y);
        const double default_width = static_cast<double>(source_bounds.max_x - source_bounds.min_x + 1);
        const double default_height = static_cast<double>(source_bounds.max_y - source_bounds.min_y + 1);
        const double x = ParseSVGLengthAttr(primitive.attributes, "x", default_x, SvgLengthAxis::kX, viewport_width, viewport_height);
        const double y = ParseSVGLengthAttr(primitive.attributes, "y", default_y, SvgLengthAxis::kY, viewport_width, viewport_height);
        const double width = ParseSVGLengthAttr(primitive.attributes,
                                                "width",
                                                default_width,
                                                SvgLengthAxis::kX,
                                                viewport_width,
                                                viewport_height);
        const double height = ParseSVGLengthAttr(primitive.attributes,
                                                 "height",
                                                 default_height,
                                                 SvgLengthAxis::kY,
                                                 viewport_width,
                                                 viewport_height);
        DrawCGImageToSurface(output, image, x, y, width, height);
        CGImageRelease(image);
        return output;
    }

    CGImageRef image = LoadImageFromHref(*href);
    if (image == nullptr) {
        return output;
    }
    const double viewport_width = static_cast<double>(std::max<size_t>(1, source_surface.width));
    const double viewport_height = static_cast<double>(std::max<size_t>(1, source_surface.height));
    const double x = ParseSVGLengthAttr(primitive.attributes, "x", 0.0, SvgLengthAxis::kX, viewport_width, viewport_height);
    const double y = ParseSVGLengthAttr(primitive.attributes, "y", 0.0, SvgLengthAxis::kY, viewport_width, viewport_height);
    const double width = ParseSVGLengthAttr(primitive.attributes,
                                            "width",
                                            static_cast<double>(CGImageGetWidth(image)),
                                            SvgLengthAxis::kX,
                                            viewport_width,
                                            viewport_height);
    const double height = ParseSVGLengthAttr(primitive.attributes,
                                             "height",
                                             static_cast<double>(CGImageGetHeight(image)),
                                             SvgLengthAxis::kY,
                                             viewport_width,
                                             viewport_height);
    DrawCGImageToSurface(output, image, x, y, width, height);
    CGImageRelease(image);
    return output;
}

size_t ResolveChannelSelector(const std::string& selector) {
    const std::string normalized = Lower(Trim(selector));
    if (normalized == "r") {
        return 0;
    }
    if (normalized == "g") {
        return 1;
    }
    if (normalized == "b") {
        return 2;
    }
    return 3;
}

PixelSurface ApplyOffsetFilter(const PixelSurface& input, double dx, double dy) {
    PixelSurface output = MakeTransparentSurface(input.width, input.height);
    if (input.width == 0 || input.height == 0) {
        return output;
    }

    const int offset_x = static_cast<int>(std::lround(dx));
    const int offset_y = static_cast<int>(std::lround(dy));
    const int width = static_cast<int>(input.width);
    const int height = static_cast<int>(input.height);

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const int src_x = x - offset_x;
            const int src_y = y - offset_y;
            if (src_x < 0 || src_y < 0 || src_x >= width || src_y >= height) {
                continue;
            }
            const size_t src_base = (static_cast<size_t>(src_y) * input.width + static_cast<size_t>(src_x)) * 4;
            const size_t dst_base = (static_cast<size_t>(y) * output.width + static_cast<size_t>(x)) * 4;
            output.rgba[dst_base + 0] = input.rgba[src_base + 0];
            output.rgba[dst_base + 1] = input.rgba[src_base + 1];
            output.rgba[dst_base + 2] = input.rgba[src_base + 2];
            output.rgba[dst_base + 3] = input.rgba[src_base + 3];
        }
    }
    return output;
}

std::vector<double> BuildGaussianKernel(double std_deviation) {
    if (std_deviation <= 0.0) {
        return {1.0};
    }

    const int radius = std::max(1, static_cast<int>(std::ceil(std_deviation * 3.0)));
    std::vector<double> kernel(static_cast<size_t>(radius * 2 + 1), 0.0);
    const double two_sigma_squared = 2.0 * std_deviation * std_deviation;
    double sum = 0.0;
    for (int i = -radius; i <= radius; ++i) {
        const double weight = std::exp(-(static_cast<double>(i * i)) / two_sigma_squared);
        kernel[static_cast<size_t>(i + radius)] = weight;
        sum += weight;
    }
    if (sum > 0.0) {
        for (double& value : kernel) {
            value /= sum;
        }
    }
    return kernel;
}

PixelSurface Convolve1D(const PixelSurface& input, const std::vector<double>& kernel, bool horizontal) {
    if (input.width == 0 || input.height == 0 || kernel.empty()) {
        return input;
    }

    PixelSurface output = MakeTransparentSurface(input.width, input.height);
    const int radius = static_cast<int>(kernel.size() / 2);
    const int width = static_cast<int>(input.width);
    const int height = static_cast<int>(input.height);

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            double accum[4] = {0.0, 0.0, 0.0, 0.0};
            double weight_sum = 0.0;
            for (int k = -radius; k <= radius; ++k) {
                const int sample_x = horizontal ? (x + k) : x;
                const int sample_y = horizontal ? y : (y + k);
                if (sample_x < 0 || sample_y < 0 || sample_x >= width || sample_y >= height) {
                    continue;
                }
                const double weight = kernel[static_cast<size_t>(k + radius)];
                const size_t sample_base = (static_cast<size_t>(sample_y) * input.width + static_cast<size_t>(sample_x)) * 4;
                for (size_t channel = 0; channel < 4; ++channel) {
                    accum[channel] += (static_cast<double>(input.rgba[sample_base + channel]) / 255.0) * weight;
                }
                weight_sum += weight;
            }

            if (weight_sum > 0.0) {
                for (double& channel_value : accum) {
                    channel_value /= weight_sum;
                }
            }

            const size_t out_base = (static_cast<size_t>(y) * output.width + static_cast<size_t>(x)) * 4;
            for (size_t channel = 0; channel < 4; ++channel) {
                output.rgba[out_base + channel] = static_cast<uint8_t>(std::lround(std::clamp(accum[channel], 0.0, 1.0) * 255.0));
            }
        }
    }
    return output;
}

PixelSurface ApplyGaussianBlurFilter(const PixelSurface& input, double std_x, double std_y) {
    if (std_x <= 0.0 && std_y <= 0.0) {
        return input;
    }

    PixelSurface horizontal = input;
    if (std_x > 0.0) {
        horizontal = Convolve1D(input, BuildGaussianKernel(std_x), true);
    }

    if (std_y > 0.0) {
        return Convolve1D(horizontal, BuildGaussianKernel(std_y), false);
    }
    return horizontal;
}

struct ChannelTransferFunction {
    enum class Type {
        kIdentity,
        kTable,
        kDiscrete,
        kLinear,
        kGamma,
    };

    Type type = Type::kIdentity;
    std::vector<double> table_values;
    double slope = 1.0;
    double intercept = 0.0;
    double amplitude = 1.0;
    double exponent = 1.0;
    double offset = 0.0;
};

ChannelTransferFunction ParseTransferFunction(const XmlNode& node) {
    ChannelTransferFunction fn;
    const std::string type = Lower(Trim(node.attributes.count("type") ? node.attributes.at("type") : "identity"));
    if (type == "table") {
        fn.type = ChannelTransferFunction::Type::kTable;
        fn.table_values = ParseNumberList(node.attributes.count("tableValues") ? node.attributes.at("tableValues") : "");
    } else if (type == "discrete") {
        fn.type = ChannelTransferFunction::Type::kDiscrete;
        fn.table_values = ParseNumberList(node.attributes.count("tableValues") ? node.attributes.at("tableValues") : "");
    } else if (type == "linear") {
        fn.type = ChannelTransferFunction::Type::kLinear;
        fn.slope = ParseDouble(node.attributes.count("slope") ? node.attributes.at("slope") : "", 1.0);
        fn.intercept = ParseDouble(node.attributes.count("intercept") ? node.attributes.at("intercept") : "", 0.0);
    } else if (type == "gamma") {
        fn.type = ChannelTransferFunction::Type::kGamma;
        fn.amplitude = ParseDouble(node.attributes.count("amplitude") ? node.attributes.at("amplitude") : "", 1.0);
        fn.exponent = ParseDouble(node.attributes.count("exponent") ? node.attributes.at("exponent") : "", 1.0);
        fn.offset = ParseDouble(node.attributes.count("offset") ? node.attributes.at("offset") : "", 0.0);
    }
    return fn;
}

double EvaluateTransferFunction(const ChannelTransferFunction& fn, double value) {
    const double x = std::clamp(value, 0.0, 1.0);
    switch (fn.type) {
        case ChannelTransferFunction::Type::kIdentity:
            return x;
        case ChannelTransferFunction::Type::kTable: {
            if (fn.table_values.empty()) {
                return x;
            }
            if (fn.table_values.size() == 1) {
                return std::clamp(fn.table_values[0], 0.0, 1.0);
            }
            const double pos = x * static_cast<double>(fn.table_values.size() - 1);
            const size_t index = static_cast<size_t>(std::floor(pos));
            if (index >= fn.table_values.size() - 1) {
                return std::clamp(fn.table_values.back(), 0.0, 1.0);
            }
            const double t = pos - static_cast<double>(index);
            const double a = fn.table_values[index];
            const double b = fn.table_values[index + 1];
            return std::clamp(a + (b - a) * t, 0.0, 1.0);
        }
        case ChannelTransferFunction::Type::kDiscrete: {
            if (fn.table_values.empty()) {
                return x;
            }
            const size_t count = fn.table_values.size();
            size_t index = static_cast<size_t>(std::floor(x * static_cast<double>(count)));
            if (index >= count) {
                index = count - 1;
            }
            return std::clamp(fn.table_values[index], 0.0, 1.0);
        }
        case ChannelTransferFunction::Type::kLinear:
            return std::clamp((fn.slope * x) + fn.intercept, 0.0, 1.0);
        case ChannelTransferFunction::Type::kGamma:
            return std::clamp((fn.amplitude * std::pow(std::max(0.0, x), fn.exponent)) + fn.offset, 0.0, 1.0);
    }
    return x;
}

PixelSurface ApplyComponentTransferFilter(const PixelSurface& input, const XmlNode& primitive) {
    ChannelTransferFunction fn_r;
    ChannelTransferFunction fn_g;
    ChannelTransferFunction fn_b;
    ChannelTransferFunction fn_a;

    for (const auto& child : primitive.children) {
        const std::string name = LocalName(child.name);
        if (name == "funca" || name == "fefunca") {
            fn_a = ParseTransferFunction(child);
        } else if (name == "funcr" || name == "fefuncr") {
            fn_r = ParseTransferFunction(child);
        } else if (name == "funcg" || name == "fefuncg") {
            fn_g = ParseTransferFunction(child);
        } else if (name == "funcb" || name == "fefuncb") {
            fn_b = ParseTransferFunction(child);
        }
    }

    PixelSurface output = input;
    for (size_t i = 0; i < input.width * input.height; ++i) {
        const size_t base = i * 4;
        const auto src = ReadPixelStraightLinear(input, base);
        RGBAColor dst;
        dst.r = EvaluateTransferFunction(fn_r, src.r);
        dst.g = EvaluateTransferFunction(fn_g, src.g);
        dst.b = EvaluateTransferFunction(fn_b, src.b);
        dst.a = EvaluateTransferFunction(fn_a, src.a);
        WritePixelFromStraightLinear(output, base, dst);
    }
    return output;
}

int PositiveModulo(int value, int mod) {
    if (mod <= 0) {
        return 0;
    }
    int result = value % mod;
    if (result < 0) {
        result += mod;
    }
    return result;
}

RGBAColor SampleLinearRGBA(const PixelSurface& surface, int x, int y, const std::string& edge_mode) {
    const int width = static_cast<int>(surface.width);
    const int height = static_cast<int>(surface.height);
    if (width <= 0 || height <= 0) {
        return {};
    }

    int sx = x;
    int sy = y;
    const std::string mode = Lower(edge_mode);

    if (mode == "wrap") {
        sx = PositiveModulo(sx, width);
        sy = PositiveModulo(sy, height);
    } else if (mode == "duplicate") {
        sx = std::clamp(sx, 0, width - 1);
        sy = std::clamp(sy, 0, height - 1);
    } else if (sx < 0 || sy < 0 || sx >= width || sy >= height) {
        return {};
    }

    const size_t base = (static_cast<size_t>(sy) * surface.width + static_cast<size_t>(sx)) * 4;
    return ReadPixelStraightLinear(surface, base);
}

PixelSurface ApplyConvolveMatrixFilter(const PixelSurface& input, const XmlNode& primitive) {
    const auto order_values = ParseNumberList(primitive.attributes.count("order") ? primitive.attributes.at("order") : "");
    const int order_x = std::max(1, static_cast<int>(std::lround(order_values.empty() ? 3.0 : order_values[0])));
    const int order_y = std::max(1, static_cast<int>(std::lround(order_values.size() > 1 ? order_values[1] : static_cast<double>(order_x))));
    const int kernel_size = order_x * order_y;

    std::vector<double> kernel = ParseNumberList(primitive.attributes.count("kernelMatrix") ? primitive.attributes.at("kernelMatrix") : "");
    if (static_cast<int>(kernel.size()) < kernel_size) {
        kernel.assign(static_cast<size_t>(kernel_size), 0.0);
        const int center = (order_y / 2) * order_x + (order_x / 2);
        if (center >= 0 && center < kernel_size) {
            kernel[static_cast<size_t>(center)] = 1.0;
        }
    } else if (static_cast<int>(kernel.size()) > kernel_size) {
        kernel.resize(static_cast<size_t>(kernel_size));
    }

    const double kernel_sum = std::accumulate(kernel.begin(), kernel.end(), 0.0);
    const double divisor = ParseDouble(primitive.attributes.count("divisor") ? primitive.attributes.at("divisor") : "",
                                       std::abs(kernel_sum) > 1e-9 ? kernel_sum : 1.0);
    const double safe_divisor = std::abs(divisor) > 1e-9 ? divisor : 1.0;
    const double bias = ParseDouble(primitive.attributes.count("bias") ? primitive.attributes.at("bias") : "", 0.0);
    const int target_x = static_cast<int>(std::lround(ParseDouble(primitive.attributes.count("targetX") ? primitive.attributes.at("targetX") : "",
                                                                   static_cast<double>(order_x / 2))));
    const int target_y = static_cast<int>(std::lround(ParseDouble(primitive.attributes.count("targetY") ? primitive.attributes.at("targetY") : "",
                                                                   static_cast<double>(order_y / 2))));
    const bool preserve_alpha = Lower(Trim(primitive.attributes.count("preserveAlpha") ? primitive.attributes.at("preserveAlpha") : "false")) == "true";
    const std::string edge_mode = primitive.attributes.count("edgeMode") ? primitive.attributes.at("edgeMode") : "duplicate";

    PixelSurface output = input;
    const int width = static_cast<int>(input.width);
    const int height = static_cast<int>(input.height);

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            RGBAColor accum;
            for (int ky = 0; ky < order_y; ++ky) {
                for (int kx = 0; kx < order_x; ++kx) {
                    const int sample_x = x + (kx - target_x);
                    const int sample_y = y + (ky - target_y);
                    const double weight = kernel[static_cast<size_t>(ky * order_x + kx)];
                    const auto sample = SampleLinearRGBA(input, sample_x, sample_y, edge_mode);
                    accum.r += sample.r * weight;
                    accum.g += sample.g * weight;
                    accum.b += sample.b * weight;
                    accum.a += sample.a * weight;
                }
            }

            RGBAColor dst;
            dst.r = (accum.r / safe_divisor) + bias;
            dst.g = (accum.g / safe_divisor) + bias;
            dst.b = (accum.b / safe_divisor) + bias;
            dst.a = preserve_alpha
                ? ReadPixelStraightLinear(input, (static_cast<size_t>(y) * input.width + static_cast<size_t>(x)) * 4).a
                : ((accum.a / safe_divisor) + bias);

            const size_t base = (static_cast<size_t>(y) * output.width + static_cast<size_t>(x)) * 4;
            WritePixelFromStraightLinear(output, base, dst);
        }
    }
    return output;
}

PixelSurface ApplyMorphologyFilter(const PixelSurface& input, const XmlNode& primitive) {
    const auto radius_values = ParseNumberList(primitive.attributes.count("radius") ? primitive.attributes.at("radius") : "");
    const int radius_x = std::max(0, static_cast<int>(std::lround(radius_values.empty() ? 0.0 : radius_values[0])));
    const int radius_y = std::max(0, static_cast<int>(std::lround(radius_values.size() > 1 ? radius_values[1] : static_cast<double>(radius_x))));
    if (radius_x == 0 && radius_y == 0) {
        return input;
    }

    const bool dilate = Lower(Trim(primitive.attributes.count("operator") ? primitive.attributes.at("operator") : "erode")) == "dilate";
    PixelSurface output = input;
    const int width = static_cast<int>(input.width);
    const int height = static_cast<int>(input.height);

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            int extrema[4] = {dilate ? 0 : 255, dilate ? 0 : 255, dilate ? 0 : 255, dilate ? 0 : 255};
            for (int dy = -radius_y; dy <= radius_y; ++dy) {
                for (int dx = -radius_x; dx <= radius_x; ++dx) {
                    const int sx = std::clamp(x + dx, 0, width - 1);
                    const int sy = std::clamp(y + dy, 0, height - 1);
                    const size_t base = (static_cast<size_t>(sy) * input.width + static_cast<size_t>(sx)) * 4;
                    for (size_t channel = 0; channel < 4; ++channel) {
                        const int sample = static_cast<int>(input.rgba[base + channel]);
                        extrema[channel] = dilate ? std::max(extrema[channel], sample) : std::min(extrema[channel], sample);
                    }
                }
            }

            const size_t out_base = (static_cast<size_t>(y) * output.width + static_cast<size_t>(x)) * 4;
            for (size_t channel = 0; channel < 4; ++channel) {
                output.rgba[out_base + channel] = static_cast<uint8_t>(std::clamp(extrema[channel], 0, 255));
            }
        }
    }
    return output;
}

PixelSurface ApplyTileFilter(const PixelSurface& input, const XmlNode& primitive) {
    PixelSurface output = MakeTransparentSurface(input.width, input.height);
    if (input.width == 0 || input.height == 0) {
        return output;
    }

    const PixelBounds source_bounds = ResolveUsableBounds(input, ComputeNonTransparentBounds(input));
    const int tile_width = static_cast<int>(source_bounds.max_x - source_bounds.min_x + 1);
    const int tile_height = static_cast<int>(source_bounds.max_y - source_bounds.min_y + 1);
    if (tile_width <= 0 || tile_height <= 0) {
        return output;
    }

    const double viewport_width = static_cast<double>(std::max<size_t>(1, input.width));
    const double viewport_height = static_cast<double>(std::max<size_t>(1, input.height));
    const double region_x = ParseSVGLengthAttr(primitive.attributes, "x", 0.0, SvgLengthAxis::kX, viewport_width, viewport_height);
    const double region_y = ParseSVGLengthAttr(primitive.attributes, "y", 0.0, SvgLengthAxis::kY, viewport_width, viewport_height);
    const double region_w = ParseSVGLengthAttr(primitive.attributes, "width", viewport_width, SvgLengthAxis::kX, viewport_width, viewport_height);
    const double region_h = ParseSVGLengthAttr(primitive.attributes, "height", viewport_height, SvgLengthAxis::kY, viewport_width, viewport_height);

    const int start_x = std::max(0, static_cast<int>(std::floor(region_x)));
    const int start_y = std::max(0, static_cast<int>(std::floor(region_y)));
    const int end_x = std::min(static_cast<int>(input.width), static_cast<int>(std::ceil(region_x + region_w)));
    const int end_y = std::min(static_cast<int>(input.height), static_cast<int>(std::ceil(region_y + region_h)));

    for (int y = start_y; y < end_y; ++y) {
        for (int x = start_x; x < end_x; ++x) {
            const int local_x = PositiveModulo(x - start_x, tile_width);
            const int local_y = PositiveModulo(y - start_y, tile_height);
            const int sx = static_cast<int>(source_bounds.min_x) + local_x;
            const int sy = static_cast<int>(source_bounds.min_y) + local_y;
            const size_t src_base = (static_cast<size_t>(sy) * input.width + static_cast<size_t>(sx)) * 4;
            const size_t dst_base = (static_cast<size_t>(y) * output.width + static_cast<size_t>(x)) * 4;
            output.rgba[dst_base + 0] = input.rgba[src_base + 0];
            output.rgba[dst_base + 1] = input.rgba[src_base + 1];
            output.rgba[dst_base + 2] = input.rgba[src_base + 2];
            output.rgba[dst_base + 3] = input.rgba[src_base + 3];
        }
    }
    return output;
}

double SmoothStep(double value) {
    const double t = std::clamp(value, 0.0, 1.0);
    return t * t * (3.0 - (2.0 * t));
}

double NoiseHash(int x, int y, int seed) {
    uint32_t n = static_cast<uint32_t>(x) * 374761393u;
    n ^= static_cast<uint32_t>(y) * 668265263u;
    n ^= static_cast<uint32_t>(seed) * 2246822519u;
    n = (n ^ (n >> 13u)) * 1274126177u;
    n ^= (n >> 16u);
    return (static_cast<double>(n & 0x00FFFFFFu) / static_cast<double>(0x007FFFFFu)) - 1.0;
}

double ValueNoise(double x, double y, int seed) {
    const int x0 = static_cast<int>(std::floor(x));
    const int y0 = static_cast<int>(std::floor(y));
    const int x1 = x0 + 1;
    const int y1 = y0 + 1;
    const double tx = SmoothStep(x - static_cast<double>(x0));
    const double ty = SmoothStep(y - static_cast<double>(y0));

    const double n00 = NoiseHash(x0, y0, seed);
    const double n10 = NoiseHash(x1, y0, seed);
    const double n01 = NoiseHash(x0, y1, seed);
    const double n11 = NoiseHash(x1, y1, seed);

    const double nx0 = n00 + (n10 - n00) * tx;
    const double nx1 = n01 + (n11 - n01) * tx;
    return nx0 + (nx1 - nx0) * ty;
}

double FractalNoise(double x, double y, int seed, int octaves, bool turbulence) {
    double value = 0.0;
    double amplitude = 1.0;
    double amplitude_sum = 0.0;
    double frequency = 1.0;

    for (int octave = 0; octave < octaves; ++octave) {
        double n = ValueNoise(x * frequency, y * frequency, seed + octave * 31);
        if (turbulence) {
            n = std::abs(n);
        }
        value += n * amplitude;
        amplitude_sum += amplitude;
        amplitude *= 0.5;
        frequency *= 2.0;
    }

    if (amplitude_sum <= 0.0) {
        return 0.0;
    }
    value /= amplitude_sum;
    if (!turbulence) {
        value = (value * 0.5) + 0.5;
    }
    return std::clamp(value, 0.0, 1.0);
}

PixelSurface ApplyTurbulenceFilter(const PixelSurface& source_surface, const XmlNode& primitive) {
    PixelSurface output = MakeTransparentSurface(source_surface.width, source_surface.height);
    if (source_surface.width == 0 || source_surface.height == 0) {
        return output;
    }

    const auto base_values = ParseNumberList(primitive.attributes.count("baseFrequency") ? primitive.attributes.at("baseFrequency") : "");
    double fx = base_values.empty() ? 0.0 : base_values[0];
    double fy = base_values.size() > 1 ? base_values[1] : fx;
    fx = std::max(fx, 0.0);
    fy = std::max(fy, 0.0);
    if (fx <= 0.0 && fy <= 0.0) {
        return output;
    }
    if (fx <= 0.0) {
        fx = fy;
    }
    if (fy <= 0.0) {
        fy = fx;
    }

    const int octaves = std::clamp(static_cast<int>(std::lround(ParseDouble(primitive.attributes.count("numOctaves") ? primitive.attributes.at("numOctaves") : "", 1.0))), 1, 8);
    const int seed = static_cast<int>(std::lround(ParseDouble(primitive.attributes.count("seed") ? primitive.attributes.at("seed") : "", 0.0)));
    const bool turbulence = Lower(Trim(primitive.attributes.count("type") ? primitive.attributes.at("type") : "turbulence")) != "fractalnoise";

    for (size_t y = 0; y < output.height; ++y) {
        for (size_t x = 0; x < output.width; ++x) {
            const double nx = static_cast<double>(x) * fx;
            const double ny = static_cast<double>(y) * fy;
            const double r = FractalNoise(nx, ny, seed + 11, octaves, turbulence);
            const double g = FractalNoise(nx, ny, seed + 37, octaves, turbulence);
            const double b = FractalNoise(nx, ny, seed + 73, octaves, turbulence);
            const size_t base = (y * output.width + x) * 4;
            output.rgba[base + 0] = static_cast<uint8_t>(std::lround(r * 255.0));
            output.rgba[base + 1] = static_cast<uint8_t>(std::lround(g * 255.0));
            output.rgba[base + 2] = static_cast<uint8_t>(std::lround(b * 255.0));
            output.rgba[base + 3] = 255;
        }
    }
    return output;
}

RGBAColor SampleNearestLinear(const PixelSurface& surface, double x, double y) {
    const int sx = static_cast<int>(std::lround(x));
    const int sy = static_cast<int>(std::lround(y));
    if (sx < 0 || sy < 0 || sx >= static_cast<int>(surface.width) || sy >= static_cast<int>(surface.height)) {
        return {};
    }
    const size_t base = (static_cast<size_t>(sy) * surface.width + static_cast<size_t>(sx)) * 4;
    return ReadPixelStraightLinear(surface, base);
}

PixelSurface ApplyDisplacementMapFilter(const PixelSurface& input, const PixelSurface& map_surface, const XmlNode& primitive) {
    PixelSurface output = MakeTransparentSurface(input.width, input.height);
    const double scale = ParseDouble(primitive.attributes.count("scale") ? primitive.attributes.at("scale") : "", 0.0);
    const size_t channel_x = ResolveChannelSelector(primitive.attributes.count("xChannelSelector") ? primitive.attributes.at("xChannelSelector") : "A");
    const size_t channel_y = ResolveChannelSelector(primitive.attributes.count("yChannelSelector") ? primitive.attributes.at("yChannelSelector") : "A");

    for (size_t y = 0; y < output.height; ++y) {
        for (size_t x = 0; x < output.width; ++x) {
            const size_t map_base = (y * map_surface.width + x) * 4;
            const auto map_color = ReadPixelStraightLinear(map_surface, map_base);
            const double channel_values[4] = {map_color.r, map_color.g, map_color.b, map_color.a};
            const double dx = scale * (channel_values[channel_x] - 0.5);
            const double dy = scale * (channel_values[channel_y] - 0.5);
            const auto sample = SampleNearestLinear(input, static_cast<double>(x) + dx, static_cast<double>(y) + dy);
            const size_t out_base = (y * output.width + x) * 4;
            WritePixelFromStraightLinear(output, out_base, sample);
        }
    }
    return output;
}

std::array<double, 3> ParseDistantLightDirection(const XmlNode& primitive) {
    for (const auto& child : primitive.children) {
        if (LocalName(child.name) != "fedistantlight") {
            continue;
        }
        const double azimuth = ParseDouble(child.attributes.count("azimuth") ? child.attributes.at("azimuth") : "", 0.0) * M_PI / 180.0;
        const double elevation = ParseDouble(child.attributes.count("elevation") ? child.attributes.at("elevation") : "", 0.0) * M_PI / 180.0;
        const double x = std::cos(elevation) * std::cos(azimuth);
        const double y = std::cos(elevation) * std::sin(azimuth);
        const double z = std::sin(elevation);
        return {x, y, z};
    }
    return {0.0, 0.0, 1.0};
}

std::array<double, 3> Normalize3(double x, double y, double z) {
    const double length = std::sqrt((x * x) + (y * y) + (z * z));
    if (length <= 1e-9) {
        return {0.0, 0.0, 1.0};
    }
    return {x / length, y / length, z / length};
}

PixelSurface ApplyLightingFilter(const PixelSurface& input, const XmlNode& primitive, bool specular) {
    PixelSurface output = MakeTransparentSurface(input.width, input.height);
    if (input.width == 0 || input.height == 0) {
        return output;
    }

    const auto light_dir_raw = ParseDistantLightDirection(primitive);
    const auto light_dir = Normalize3(light_dir_raw[0], light_dir_raw[1], light_dir_raw[2]);
    const double surface_scale = ParseDouble(primitive.attributes.count("surfaceScale") ? primitive.attributes.at("surfaceScale") : "", 1.0);
    const double diffuse_constant = ParseDouble(primitive.attributes.count("diffuseConstant") ? primitive.attributes.at("diffuseConstant") : "", 1.0);
    const double specular_constant = ParseDouble(primitive.attributes.count("specularConstant") ? primitive.attributes.at("specularConstant") : "", 1.0);
    const double specular_exponent = std::clamp(ParseDouble(primitive.attributes.count("specularExponent") ? primitive.attributes.at("specularExponent") : "", 1.0), 1.0, 128.0);

    const auto style_it = primitive.attributes.find("style");
    const auto inline_style = style_it != primitive.attributes.end()
        ? ParseInlineStyle(style_it->second)
        : std::map<std::string, std::string>{};
    Color light_color = StyleResolver::ParseColor(ReadAttrOrStyle(primitive, inline_style, "lighting-color").value_or("white"));
    if (!light_color.is_valid || light_color.is_none) {
        light_color = StyleResolver::ParseColor("white");
    }

    const auto alpha_at = [&](int x, int y) -> double {
        x = std::clamp(x, 0, static_cast<int>(input.width) - 1);
        y = std::clamp(y, 0, static_cast<int>(input.height) - 1);
        const size_t base = (static_cast<size_t>(y) * input.width + static_cast<size_t>(x)) * 4;
        return (static_cast<double>(input.rgba[base + 3]) / 255.0) * surface_scale;
    };

    for (int y = 0; y < static_cast<int>(output.height); ++y) {
        for (int x = 0; x < static_cast<int>(output.width); ++x) {
            const double height_left = alpha_at(x - 1, y);
            const double height_right = alpha_at(x + 1, y);
            const double height_up = alpha_at(x, y - 1);
            const double height_down = alpha_at(x, y + 1);
            const auto normal = Normalize3(-(height_right - height_left) * 0.5,
                                           -(height_down - height_up) * 0.5,
                                           1.0);

            double intensity = 0.0;
            const double ndotl = std::max(0.0, normal[0] * light_dir[0] + normal[1] * light_dir[1] + normal[2] * light_dir[2]);
            if (specular) {
                const double rx = 2.0 * ndotl * normal[0] - light_dir[0];
                const double ry = 2.0 * ndotl * normal[1] - light_dir[1];
                const double rz = 2.0 * ndotl * normal[2] - light_dir[2];
                const auto reflection = Normalize3(rx, ry, rz);
                intensity = specular_constant * std::pow(std::max(0.0, reflection[2]), specular_exponent);
            } else {
                intensity = diffuse_constant * ndotl;
            }
            intensity = std::clamp(intensity, 0.0, 1.0);

            const size_t base = (static_cast<size_t>(y) * output.width + static_cast<size_t>(x)) * 4;
            const double alpha = static_cast<double>(input.rgba[base + 3]) / 255.0;
            output.rgba[base + 0] = static_cast<uint8_t>(std::lround(std::clamp(light_color.r * intensity * alpha, 0.0, 1.0) * 255.0));
            output.rgba[base + 1] = static_cast<uint8_t>(std::lround(std::clamp(light_color.g * intensity * alpha, 0.0, 1.0) * 255.0));
            output.rgba[base + 2] = static_cast<uint8_t>(std::lround(std::clamp(light_color.b * intensity * alpha, 0.0, 1.0) * 255.0));
            output.rgba[base + 3] = static_cast<uint8_t>(std::lround(alpha * 255.0));
        }
    }
    return output;
}

std::optional<PixelSurface> ExecuteBasicFilterPrimitives(const XmlNode& filter_node,
                                                         const PixelSurface& source_surface,
                                                         const StyleResolver& style_resolver,
                                                         const GeometryEngine& geometry_engine,
                                                         const GradientMap& gradients,
                                                         const PatternMap& patterns,
                                                         const NodeIdMap& id_map,
                                                         const ColorProfileMap& color_profiles,
                                                         const RenderOptions& options,
                                                         RenderError& error) {
    std::map<std::string, PixelSurface> surfaces;
    surfaces["SourceGraphic"] = source_surface;
    PixelSurface source_alpha = source_surface;
    for (size_t i = 0; i < source_alpha.width * source_alpha.height; ++i) {
        const size_t base = i * 4;
        const uint8_t alpha = source_alpha.rgba[base + 3];
        source_alpha.rgba[base + 0] = 0;
        source_alpha.rgba[base + 1] = 0;
        source_alpha.rgba[base + 2] = 0;
        source_alpha.rgba[base + 3] = alpha;
    }
    surfaces["SourceAlpha"] = std::move(source_alpha);
    std::string last_key = "SourceGraphic";
    size_t unnamed_index = 0;
    const PixelBounds filter_bounds = ComputeNonTransparentBounds(source_surface);

    const auto resolve_input = [&](const std::string& key) -> const PixelSurface* {
        const auto it = surfaces.find(key);
        if (it != surfaces.end()) {
            return &it->second;
        }
        const auto last_it = surfaces.find(last_key);
        return last_it != surfaces.end() ? &last_it->second : nullptr;
    };

    for (const auto& primitive : filter_node.children) {
        const auto primitive_name = LocalName(primitive.name);
        PixelSurface output;

        if (primitive_name == "feflood") {
            output = MakeFloodSurface(source_surface.width, source_surface.height, primitive, filter_bounds);
        } else if (primitive_name == "fegaussianblur") {
            const auto stddev_values = ParseNumberList(primitive.attributes.count("stdDeviation") ? primitive.attributes.at("stdDeviation") : "");
            const double std_x = stddev_values.empty() ? 0.0 : std::max(0.0, stddev_values[0]);
            const double std_y = stddev_values.size() > 1 ? std::max(0.0, stddev_values[1]) : std_x;
            const std::string in_key = Trim(primitive.attributes.count("in") ? primitive.attributes.at("in") : "SourceGraphic");
            const auto* in_surface = resolve_input(in_key.empty() ? "SourceGraphic" : in_key);
            if (in_surface == nullptr) {
                return std::nullopt;
            }
            output = ApplyGaussianBlurFilter(*in_surface, std_x, std_y);
        } else if (primitive_name == "feoffset") {
            const std::string in_key = Trim(primitive.attributes.count("in") ? primitive.attributes.at("in") : "SourceGraphic");
            const auto* in_surface = resolve_input(in_key.empty() ? "SourceGraphic" : in_key);
            if (in_surface == nullptr) {
                return std::nullopt;
            }
            const double viewport_width = static_cast<double>(std::max<size_t>(1, in_surface->width));
            const double viewport_height = static_cast<double>(std::max<size_t>(1, in_surface->height));
            const double dx = ParseSVGLengthAttr(primitive.attributes, "dx", 0.0, SvgLengthAxis::kX, viewport_width, viewport_height);
            const double dy = ParseSVGLengthAttr(primitive.attributes, "dy", 0.0, SvgLengthAxis::kY, viewport_width, viewport_height);
            output = ApplyOffsetFilter(*in_surface, dx, dy);
        } else if (primitive_name == "feimage") {
            output = RenderImageFilterPrimitive(primitive,
                                                source_surface,
                                                style_resolver,
                                                geometry_engine,
                                                gradients,
                                                patterns,
                                                id_map,
                                                color_profiles,
                                                options,
                                                error);
        } else if (primitive_name == "fecolormatrix") {
            const std::string in_key = Trim(primitive.attributes.count("in") ? primitive.attributes.at("in") : "SourceGraphic");
            const auto* in_surface = resolve_input(in_key.empty() ? "SourceGraphic" : in_key);
            if (in_surface == nullptr) {
                return std::nullopt;
            }
            output = ApplyColorMatrix(*in_surface, primitive);
        } else if (primitive_name == "fecomponenttransfer") {
            const std::string in_key = Trim(primitive.attributes.count("in") ? primitive.attributes.at("in") : "SourceGraphic");
            const auto* in_surface = resolve_input(in_key.empty() ? "SourceGraphic" : in_key);
            if (in_surface == nullptr) {
                return std::nullopt;
            }
            output = ApplyComponentTransferFilter(*in_surface, primitive);
        } else if (primitive_name == "feconvolvematrix") {
            const std::string in_key = Trim(primitive.attributes.count("in") ? primitive.attributes.at("in") : "SourceGraphic");
            const auto* in_surface = resolve_input(in_key.empty() ? "SourceGraphic" : in_key);
            if (in_surface == nullptr) {
                return std::nullopt;
            }
            output = ApplyConvolveMatrixFilter(*in_surface, primitive);
        } else if (primitive_name == "fecomposite") {
            const std::string in_key = Trim(primitive.attributes.count("in") ? primitive.attributes.at("in") : "SourceGraphic");
            const std::string in2_key = Trim(primitive.attributes.count("in2") ? primitive.attributes.at("in2") : "SourceGraphic");
            const auto* in_surface = resolve_input(in_key.empty() ? "SourceGraphic" : in_key);
            const auto* in2_surface = resolve_input(in2_key.empty() ? "SourceGraphic" : in2_key);
            if (in_surface == nullptr || in2_surface == nullptr) {
                return std::nullopt;
            }
            const std::string op = Lower(Trim(primitive.attributes.count("operator") ? primitive.attributes.at("operator") : "over"));
            const auto parse_attr = [&](const char* key, double fallback) -> double {
                const auto it = primitive.attributes.find(key);
                if (it == primitive.attributes.end()) {
                    return fallback;
                }
                return ParseDouble(it->second, fallback);
            };
            output = CompositeSurfaces(*in_surface,
                                       *in2_surface,
                                       op,
                                       parse_attr("k1", 0.0),
                                       parse_attr("k2", 0.0),
                                       parse_attr("k3", 0.0),
                                       parse_attr("k4", 0.0));
        } else if (primitive_name == "feblend") {
            const std::string in_key = Trim(primitive.attributes.count("in") ? primitive.attributes.at("in") : "SourceGraphic");
            const std::string in2_key = Trim(primitive.attributes.count("in2") ? primitive.attributes.at("in2") : "SourceGraphic");
            const auto* in_surface = resolve_input(in_key.empty() ? "SourceGraphic" : in_key);
            const auto* in2_surface = resolve_input(in2_key.empty() ? "SourceGraphic" : in2_key);
            if (in_surface == nullptr || in2_surface == nullptr) {
                return std::nullopt;
            }

            const std::string mode = Lower(Trim(primitive.attributes.count("mode") ? primitive.attributes.at("mode") : "normal"));
            output = BlendSurfaces(*in_surface, *in2_surface, mode);
        } else if (primitive_name == "femerge") {
            output = MakeTransparentSurface(source_surface.width, source_surface.height);
            for (const auto& child : primitive.children) {
                if (LocalName(child.name) != "femergenode") {
                    continue;
                }
                const std::string in_key = Trim(child.attributes.count("in") ? child.attributes.at("in") : last_key);
                const auto* merge_surface = resolve_input(in_key.empty() ? last_key : in_key);
                if (merge_surface == nullptr) {
                    continue;
                }
                output = CompositeSurfaces(*merge_surface, output, "over");
            }
        } else if (primitive_name == "femorphology") {
            const std::string in_key = Trim(primitive.attributes.count("in") ? primitive.attributes.at("in") : "SourceGraphic");
            const auto* in_surface = resolve_input(in_key.empty() ? "SourceGraphic" : in_key);
            if (in_surface == nullptr) {
                return std::nullopt;
            }
            output = ApplyMorphologyFilter(*in_surface, primitive);
        } else if (primitive_name == "fedisplacementmap") {
            const std::string in_key = Trim(primitive.attributes.count("in") ? primitive.attributes.at("in") : "SourceGraphic");
            const std::string in2_key = Trim(primitive.attributes.count("in2") ? primitive.attributes.at("in2") : "SourceGraphic");
            const auto* in_surface = resolve_input(in_key.empty() ? "SourceGraphic" : in_key);
            const auto* in2_surface = resolve_input(in2_key.empty() ? "SourceGraphic" : in2_key);
            if (in_surface == nullptr || in2_surface == nullptr) {
                return std::nullopt;
            }
            output = ApplyDisplacementMapFilter(*in_surface, *in2_surface, primitive);
        } else if (primitive_name == "fetile") {
            const std::string in_key = Trim(primitive.attributes.count("in") ? primitive.attributes.at("in") : last_key);
            const auto* in_surface = resolve_input(in_key.empty() ? last_key : in_key);
            if (in_surface == nullptr) {
                return std::nullopt;
            }
            output = ApplyTileFilter(*in_surface, primitive);
        } else if (primitive_name == "feturbulence") {
            output = ApplyTurbulenceFilter(source_surface, primitive);
        } else if (primitive_name == "fediffuselighting") {
            const std::string in_key = Trim(primitive.attributes.count("in") ? primitive.attributes.at("in") : "SourceAlpha");
            const auto* in_surface = resolve_input(in_key.empty() ? "SourceAlpha" : in_key);
            if (in_surface == nullptr) {
                return std::nullopt;
            }
            output = ApplyLightingFilter(*in_surface, primitive, false);
        } else if (primitive_name == "fespecularlighting") {
            const std::string in_key = Trim(primitive.attributes.count("in") ? primitive.attributes.at("in") : "SourceAlpha");
            const auto* in_surface = resolve_input(in_key.empty() ? "SourceAlpha" : in_key);
            if (in_surface == nullptr) {
                return std::nullopt;
            }
            output = ApplyLightingFilter(*in_surface, primitive, true);
        } else {
            // Keep rendering moving for still-unsupported primitives.
            const auto* fallback = resolve_input(last_key);
            output = fallback != nullptr ? *fallback : source_surface;
        }

        auto result_it = primitive.attributes.find("result");
        std::string result_key;
        if (result_it != primitive.attributes.end()) {
            result_key = Trim(result_it->second);
        }
        if (result_key.empty()) {
            result_key = "__result_" + std::to_string(++unnamed_index);
        }
        surfaces[result_key] = std::move(output);
        last_key = result_key;
    }

    const auto out_it = surfaces.find(last_key);
    if (out_it == surfaces.end()) {
        return std::nullopt;
    }
    return out_it->second;
}

CGImageRef CreateImageFromSurface(const PixelSurface& surface) {
    if (surface.width == 0 || surface.height == 0 || surface.rgba.empty()) {
        return nullptr;
    }

    CGColorSpaceRef color_space = CGColorSpaceCreateDeviceRGB();
    CGContextRef bitmap = CGBitmapContextCreate(const_cast<uint8_t*>(surface.rgba.data()),
                                                surface.width,
                                                surface.height,
                                                8,
                                                surface.width * 4,
                                                color_space,
                                                static_cast<CGBitmapInfo>(kCGImageAlphaPremultipliedLast));
    CGColorSpaceRelease(color_space);
    if (bitmap == nullptr) {
        return nullptr;
    }
    CGImageRef image = CGBitmapContextCreateImage(bitmap);
    CGContextRelease(bitmap);
    return image;
}

bool PaintNodeWithFilter(const XmlNode& node,
                         const std::map<std::string, std::string>& inline_style,
                         const ResolvedStyle& node_style,
                         const StyleResolver& style_resolver,
                         const GeometryEngine& geometry_engine,
                         const ResolvedStyle* parent_style,
                         CGContextRef context,
                         const GradientMap& gradients,
                         const PatternMap& patterns,
                         const NodeIdMap& id_map,
                         const ColorProfileMap& color_profiles,
                         const RenderOptions& options,
                         RenderError& error) {
    const auto filter_id = ResolveFilterID(node, inline_style);
    if (!filter_id.has_value()) {
        return false;
    }
    const auto filter_it = id_map.find(*filter_id);
    if (filter_it == id_map.end() || filter_it->second == nullptr) {
        return false;
    }
    if (LocalName(filter_it->second->name) != "filter") {
        return false;
    }

    const auto source_surface = RenderNodeToSurface(node,
                                                    style_resolver,
                                                    geometry_engine,
                                                    parent_style,
                                                    gradients,
                                                    patterns,
                                                    id_map,
                                                    color_profiles,
                                                    options,
                                                    error);
    if (!source_surface.has_value()) {
        return false;
    }

    const auto filtered_surface = ExecuteBasicFilterPrimitives(*filter_it->second,
                                                               *source_surface,
                                                               style_resolver,
                                                               geometry_engine,
                                                               gradients,
                                                               patterns,
                                                               id_map,
                                                               color_profiles,
                                                               options,
                                                               error);
    if (!filtered_surface.has_value()) {
        return false;
    }

    CGImageRef filtered_image = CreateImageFromSurface(*filtered_surface);
    if (filtered_image == nullptr) {
        return false;
    }

    const double viewport_width = std::max(geometry_engine.viewport_width(), 1.0);
    const double viewport_height = std::max(geometry_engine.viewport_height(), 1.0);
    CGContextSaveGState(context);
    CGContextSetAlpha(context, std::clamp(node_style.opacity, 0.0f, 1.0f));
    CGContextTranslateCTM(context, 0.0, static_cast<CGFloat>(viewport_height));
    CGContextScaleCTM(context, 1.0, -1.0);
    CGContextDrawImage(context,
                       CGRectMake(0.0, 0.0, static_cast<CGFloat>(viewport_width), static_cast<CGFloat>(viewport_height)),
                       filtered_image);
    CGContextRestoreGState(context);
    CGImageRelease(filtered_image);
    return true;
}

bool PaintGradientFill(CGContextRef context,
                       CGPathRef path,
                       const ResolvedStyle& style,
                       const GradientMap& gradients,
                       double inherited_opacity) {
    const auto gradient_id = ExtractPaintURLId(style.fill_paint);
    if (!gradient_id.has_value()) {
        return false;
    }

    const auto gradient_it = gradients.find(*gradient_id);
    if (gradient_it == gradients.end()) {
        return false;
    }

    const auto& gradient_def = gradient_it->second;
    if (gradient_def.stops.empty()) {
        return false;
    }

    std::vector<CGFloat> locations;
    locations.reserve(gradient_def.stops.size());

    std::vector<CGFloat> components;
    components.reserve(gradient_def.stops.size() * 4);

    for (const auto& stop : gradient_def.stops) {
        const auto color = stop.color.is_valid ? stop.color : StyleResolver::ParseColor("black");
        const double alpha = std::clamp(color.a * stop.opacity * inherited_opacity, 0.0, 1.0);

        locations.push_back(static_cast<CGFloat>(std::clamp(stop.offset, 0.0, 1.0)));
        components.push_back(static_cast<CGFloat>(color.r));
        components.push_back(static_cast<CGFloat>(color.g));
        components.push_back(static_cast<CGFloat>(color.b));
        components.push_back(static_cast<CGFloat>(alpha));
    }

    CGColorSpaceRef color_space = CGColorSpaceCreateDeviceRGB();
    CGGradientRef gradient = CGGradientCreateWithColorComponents(color_space,
                                                                 components.data(),
                                                                 locations.data(),
                                                                 locations.size());
    CGColorSpaceRelease(color_space);

    if (gradient == nullptr) {
        return false;
    }

    const CGRect bbox = CGPathGetPathBoundingBox(path);
    const auto resolve_x = [&](double value) -> double {
        return gradient_def.user_space_units ? value : (bbox.origin.x + value * bbox.size.width);
    };
    const auto resolve_y = [&](double value) -> double {
        return gradient_def.user_space_units ? value : (bbox.origin.y + value * bbox.size.height);
    };

    CGContextSaveGState(context);
    CGContextAddPath(context, path);
    CGContextClip(context);

    if (!CGAffineTransformEqualToTransform(gradient_def.transform, CGAffineTransformIdentity)) {
        CGContextConcatCTM(context, gradient_def.transform);
    }

    if (gradient_def.type == GradientType::kLinear) {
        const CGPoint start = CGPointMake(static_cast<CGFloat>(resolve_x(gradient_def.x1)), static_cast<CGFloat>(resolve_y(gradient_def.y1)));
        const CGPoint end = CGPointMake(static_cast<CGFloat>(resolve_x(gradient_def.x2)), static_cast<CGFloat>(resolve_y(gradient_def.y2)));

        CGContextDrawLinearGradient(context,
                                    gradient,
                                    start,
                                    end,
                                    kCGGradientDrawsBeforeStartLocation | kCGGradientDrawsAfterEndLocation);
    } else {
        const CGPoint center = CGPointMake(static_cast<CGFloat>(resolve_x(gradient_def.cx)), static_cast<CGFloat>(resolve_y(gradient_def.cy)));
        const CGPoint focal = CGPointMake(static_cast<CGFloat>(resolve_x(gradient_def.fx)), static_cast<CGFloat>(resolve_y(gradient_def.fy)));

        const CGFloat radius = gradient_def.user_space_units
            ? static_cast<CGFloat>(gradient_def.r)
            : static_cast<CGFloat>(gradient_def.r * std::max(bbox.size.width, bbox.size.height));

        CGContextDrawRadialGradient(context,
                                    gradient,
                                    focal,
                                    0.0,
                                    center,
                                    radius,
                                    kCGGradientDrawsBeforeStartLocation | kCGGradientDrawsAfterEndLocation);
    }

    CGContextRestoreGState(context);
    CGGradientRelease(gradient);
    return true;
}

bool PaintPatternFill(CGContextRef context,
                      CGPathRef path,
                      const ResolvedStyle& style,
                      const StyleResolver& style_resolver,
                      const GeometryEngine& geometry_engine,
                      const GradientMap& gradients,
                      const PatternMap& patterns,
                      const NodeIdMap& id_map,
                      const ColorProfileMap& color_profiles,
                      std::set<std::string>& active_use_ids,
                      std::set<std::string>& active_pattern_ids,
                      const RenderOptions& options,
                      RenderError& error,
                      double inherited_opacity) {
    const auto pattern_id = ExtractPaintURLId(style.fill_paint);
    if (!pattern_id.has_value()) {
        return false;
    }

    const auto pattern_it = patterns.find(*pattern_id);
    if (pattern_it == patterns.end()) {
        return false;
    }
    if (active_pattern_ids.find(*pattern_id) != active_pattern_ids.end()) {
        return false;
    }

    const auto& pattern = pattern_it->second;
    if (pattern.node == nullptr) {
        return false;
    }

    const CGRect bbox = CGPathGetPathBoundingBox(path);
    const auto resolve_x = [&](double value) -> double {
        return pattern.pattern_units_user_space ? value : (bbox.origin.x + value * bbox.size.width);
    };
    const auto resolve_y = [&](double value) -> double {
        return pattern.pattern_units_user_space ? value : (bbox.origin.y + value * bbox.size.height);
    };
    const auto resolve_w = [&](double value) -> double {
        return pattern.pattern_units_user_space ? value : (value * bbox.size.width);
    };
    const auto resolve_h = [&](double value) -> double {
        return pattern.pattern_units_user_space ? value : (value * bbox.size.height);
    };

    const double tile_x = resolve_x(pattern.x);
    const double tile_y = resolve_y(pattern.y);
    const double tile_w = std::fabs(resolve_w(pattern.width));
    const double tile_h = std::fabs(resolve_h(pattern.height));
    if (!(tile_w > 0.0) || !(tile_h > 0.0)) {
        return false;
    }

    const size_t tile_px_w = static_cast<size_t>(std::max(1.0, std::ceil(tile_w)));
    const size_t tile_px_h = static_cast<size_t>(std::max(1.0, std::ceil(tile_h)));

    CGColorSpaceRef tile_cs = CGColorSpaceCreateDeviceRGB();
    CGContextRef tile_context = CGBitmapContextCreate(nullptr,
                                                      tile_px_w,
                                                      tile_px_h,
                                                      8,
                                                      0,
                                                      tile_cs,
                                                      static_cast<CGBitmapInfo>(kCGImageAlphaPremultipliedLast));
    CGColorSpaceRelease(tile_cs);
    if (tile_context == nullptr) {
        return false;
    }

    // Mirror the renderer's Y-down coordinate convention in tile space.
    CGContextTranslateCTM(tile_context, 0.0, static_cast<CGFloat>(tile_px_h));
    CGContextScaleCTM(tile_context, 1.0, -1.0);

    const CGFloat scale_x = static_cast<CGFloat>(static_cast<double>(tile_px_w) / tile_w);
    const CGFloat scale_y = static_cast<CGFloat>(static_cast<double>(tile_px_h) / tile_h);
    CGContextScaleCTM(tile_context, scale_x, scale_y);

    if (!CGAffineTransformEqualToTransform(pattern.transform, CGAffineTransformIdentity)) {
        CGContextConcatCTM(tile_context, pattern.transform);
    }

    ResolvedStyle pattern_style = style_resolver.Resolve(*pattern.node, nullptr, options);
    active_pattern_ids.insert(*pattern_id);

    if (pattern.content_units_user_space) {
        for (const auto& child : pattern.node->children) {
            PaintNode(child,
                      style_resolver,
                      geometry_engine,
                      &pattern_style,
                      tile_context,
                      gradients,
                      patterns,
                      id_map,
                      color_profiles,
                      active_use_ids,
                      active_pattern_ids,
                      options,
                      error);
            if (error.code != RenderErrorCode::kNone) {
                break;
            }
        }
    } else {
        CGContextSaveGState(tile_context);
        CGContextScaleCTM(tile_context, bbox.size.width, bbox.size.height);
        for (const auto& child : pattern.node->children) {
            PaintNode(child,
                      style_resolver,
                      geometry_engine,
                      &pattern_style,
                      tile_context,
                      gradients,
                      patterns,
                      id_map,
                      color_profiles,
                      active_use_ids,
                      active_pattern_ids,
                      options,
                      error);
            if (error.code != RenderErrorCode::kNone) {
                break;
            }
        }
        CGContextRestoreGState(tile_context);
    }

    active_pattern_ids.erase(*pattern_id);
    if (error.code != RenderErrorCode::kNone) {
        CGContextRelease(tile_context);
        return false;
    }

    CGImageRef tile_image = CGBitmapContextCreateImage(tile_context);
    CGContextRelease(tile_context);
    if (tile_image == nullptr) {
        return false;
    }

    CGContextSaveGState(context);
    CGContextAddPath(context, path);
    CGContextClip(context);
    CGContextSetAlpha(context, static_cast<CGFloat>(std::clamp(inherited_opacity, 0.0, 1.0)));

    const double start_x = tile_x + std::floor((bbox.origin.x - tile_x) / tile_w) * tile_w;
    const double start_y = tile_y + std::floor((bbox.origin.y - tile_y) / tile_h) * tile_h;
    const double end_x = bbox.origin.x + bbox.size.width;
    const double end_y = bbox.origin.y + bbox.size.height;

    for (double y = start_y; y < end_y + tile_h; y += tile_h) {
        for (double x = start_x; x < end_x + tile_w; x += tile_w) {
            const CGRect rect = CGRectMake(static_cast<CGFloat>(x),
                                           static_cast<CGFloat>(y),
                                           static_cast<CGFloat>(tile_w),
                                           static_cast<CGFloat>(tile_h));
            CGContextSaveGState(context);
            CGContextTranslateCTM(context, rect.origin.x, rect.origin.y + rect.size.height);
            CGContextScaleCTM(context, 1.0, -1.0);
            CGContextDrawImage(context, CGRectMake(0.0, 0.0, rect.size.width, rect.size.height), tile_image);
            CGContextRestoreGState(context);
        }
    }

    CGContextRestoreGState(context);
    CGImageRelease(tile_image);
    return true;
}

void CollectNodesByID(const XmlNode& node, NodeIdMap& id_map) {
    const auto id_it = node.attributes.find("id");
    if (id_it != node.attributes.end() && !id_it->second.empty()) {
        id_map.emplace(id_it->second, &node);
    }
    for (const auto& child : node.children) {
        CollectNodesByID(child, id_map);
    }
}

bool AddGeometryPath(CGContextRef context, const ShapeGeometry& geometry) {
    switch (geometry.type) {
        case ShapeType::kRect: {
            const CGRect rect = CGRectMake(static_cast<CGFloat>(geometry.x),
                                           static_cast<CGFloat>(geometry.y),
                                           static_cast<CGFloat>(geometry.width),
                                           static_cast<CGFloat>(geometry.height));
            if (geometry.rx > 0.0 || geometry.ry > 0.0) {
                const CGFloat radius = static_cast<CGFloat>(std::max(geometry.rx, geometry.ry));
                const CGPathRef path = CGPathCreateWithRoundedRect(rect, radius, radius, nullptr);
                CGContextAddPath(context, path);
                CGPathRelease(path);
            } else {
                CGContextAddRect(context, rect);
            }
            return true;
        }
        case ShapeType::kCircle: {
            const double radius = geometry.rx;
            const CGRect rect = CGRectMake(static_cast<CGFloat>(geometry.x - radius),
                                           static_cast<CGFloat>(geometry.y - radius),
                                           static_cast<CGFloat>(radius * 2.0),
                                           static_cast<CGFloat>(radius * 2.0));
            CGContextAddEllipseInRect(context, rect);
            return true;
        }
        case ShapeType::kEllipse: {
            const CGRect rect = CGRectMake(static_cast<CGFloat>(geometry.x - geometry.rx),
                                           static_cast<CGFloat>(geometry.y - geometry.ry),
                                           static_cast<CGFloat>(geometry.rx * 2.0),
                                           static_cast<CGFloat>(geometry.ry * 2.0));
            CGContextAddEllipseInRect(context, rect);
            return true;
        }
        case ShapeType::kLine: {
            if (geometry.points.size() >= 2) {
                CGContextMoveToPoint(context,
                                     static_cast<CGFloat>(geometry.points[0].x),
                                     static_cast<CGFloat>(geometry.points[0].y));
                CGContextAddLineToPoint(context,
                                        static_cast<CGFloat>(geometry.points[1].x),
                                        static_cast<CGFloat>(geometry.points[1].y));
            }
            return true;
        }
        case ShapeType::kPolygon:
        case ShapeType::kPolyline: {
            if (!geometry.points.empty()) {
                CGContextMoveToPoint(context,
                                     static_cast<CGFloat>(geometry.points[0].x),
                                     static_cast<CGFloat>(geometry.points[0].y));
                for (size_t i = 1; i < geometry.points.size(); ++i) {
                    CGContextAddLineToPoint(context,
                                            static_cast<CGFloat>(geometry.points[i].x),
                                            static_cast<CGFloat>(geometry.points[i].y));
                }
                if (geometry.type == ShapeType::kPolygon) {
                    CGContextClosePath(context);
                }
            }
            return true;
        }
        case ShapeType::kPath:
            BuildPathFromData(context, geometry.path_data);
            return true;
        case ShapeType::kText:
        case ShapeType::kImage:
        case ShapeType::kUnknown:
            return false;
    }
}

std::string ResolveTextFontFamily(const std::string& font_family) {
    std::stringstream stream(font_family);
    std::string candidate;
    while (std::getline(stream, candidate, ',')) {
        candidate = Trim(candidate);
        if (candidate.size() >= 2 &&
            ((candidate.front() == '"' && candidate.back() == '"') ||
             (candidate.front() == '\'' && candidate.back() == '\''))) {
            candidate = candidate.substr(1, candidate.size() - 2);
            candidate = Trim(candidate);
        }
        if (!candidate.empty()) {
            const std::string normalized = Lower(candidate);
            if (normalized == "sans-serif") {
                return "Helvetica";
            }
            if (normalized == "serif") {
                return "Times New Roman";
            }
            if (normalized == "monospace") {
                return "Courier";
            }
            return candidate;
        }
    }
    return "Helvetica";
}

void DrawText(CGContextRef context, const ShapeGeometry& geometry, const ResolvedStyle& style) {
    if (geometry.text.empty()) {
        return;
    }

    CFStringRef text = CFStringCreateWithCString(kCFAllocatorDefault, geometry.text.c_str(), kCFStringEncodingUTF8);
    const std::string resolved_font_family = ResolveTextFontFamily(style.font_family);
    CFStringRef font_name = CFStringCreateWithCString(kCFAllocatorDefault,
                                                      resolved_font_family.c_str(),
                                                      kCFStringEncodingUTF8);

    const CGFloat font_size = style.font_size > 0.0f ? style.font_size : 16.0f;
    CTFontRef font = CTFontCreateWithName(font_name, font_size, nullptr);
    CTFontRef draw_font = font;
    if (style.font_weight >= 600) {
        if (CTFontRef bold_font = CTFontCreateCopyWithSymbolicTraits(font,
                                                                      font_size,
                                                                      nullptr,
                                                                      kCTFontBoldTrait,
                                                                      kCTFontBoldTrait)) {
            draw_font = bold_font;
        }
    }

    CGFloat fill_r = 0.0;
    CGFloat fill_g = 0.0;
    CGFloat fill_b = 0.0;
    CGFloat fill_a = 1.0;
    if (style.fill.is_valid && !style.fill.is_none) {
        fill_r = style.fill.r;
        fill_g = style.fill.g;
        fill_b = style.fill.b;
        fill_a = std::clamp(style.fill.a * style.opacity * style.fill_opacity, 0.0f, 1.0f);
    }

    const CGFloat components[] = {fill_r, fill_g, fill_b, fill_a};
    CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
    CGColorRef cg_color = CGColorCreate(cs, components);

    CFTypeRef keys[] = {kCTFontAttributeName, kCTForegroundColorAttributeName};
    CFTypeRef values[] = {draw_font, cg_color};
    CFDictionaryRef attrs = CFDictionaryCreate(kCFAllocatorDefault,
                                               reinterpret_cast<const void**>(keys),
                                               reinterpret_cast<const void**>(values),
                                               2,
                                               &kCFTypeDictionaryKeyCallBacks,
                                               &kCFTypeDictionaryValueCallBacks);

    CFAttributedStringRef attr_string = CFAttributedStringCreate(kCFAllocatorDefault, text, attrs);
    CTLineRef line = CTLineCreateWithAttributedString(attr_string);
    const auto anchor = Lower(Trim(style.text_anchor));
    double line_width = CTLineGetTypographicBounds(line, nullptr, nullptr, nullptr);
    if (!std::isfinite(line_width)) {
        line_width = 0.0;
    }
    CGFloat anchor_offset = 0.0;
    if (anchor == "middle") {
        anchor_offset = static_cast<CGFloat>(-line_width * 0.5);
    } else if (anchor == "end") {
        anchor_offset = static_cast<CGFloat>(-line_width);
    }

    CGContextSaveGState(context);
    // CoreText glyphs are defined in a Y-up text space. Since the renderer
    // flips the global CTM to SVG's Y-down coordinates, unflip locally for
    // text so glyphs are not mirrored/inverted.
    CGContextTranslateCTM(context, static_cast<CGFloat>(geometry.x), static_cast<CGFloat>(geometry.y));
    CGContextScaleCTM(context, 1.0, -1.0);
    CGContextSetTextMatrix(context, CGAffineTransformIdentity);
    CGContextSetTextPosition(context, anchor_offset, 0.0);
    CTLineDraw(line, context);
    CGContextRestoreGState(context);

    CFRelease(line);
    CFRelease(attr_string);
    CFRelease(attrs);
    CGColorRelease(cg_color);
    CGColorSpaceRelease(cs);
    if (draw_font != font) {
        CFRelease(draw_font);
    }
    CFRelease(font);
    CFRelease(font_name);
    CFRelease(text);
}

std::string LocalName(const std::string& name) {
    const auto separator = name.rfind(':');
    if (separator == std::string::npos) {
        return Lower(name);
    }
    return Lower(name.substr(separator + 1));
}

struct RegionDatum {
    std::string name;
    double value = 0.0;
};

std::vector<RegionDatum> ParseBusinessRegions(const XmlNode& results_node) {
    std::vector<RegionDatum> regions;
    for (const auto& child : results_node.children) {
        if (LocalName(child.name) != "region") {
            continue;
        }

        RegionDatum datum;
        bool has_name = false;
        bool has_value = false;
        for (const auto& field : child.children) {
            const auto field_name = LocalName(field.name);
            if (field_name == "regionname") {
                datum.name = Trim(field.text);
                has_name = !datum.name.empty();
            } else if (field_name == "regionresult") {
                datum.value = ParseDouble(field.text, 0.0);
                has_value = true;
            }
        }

        if (has_name && has_value && datum.value > 0.0) {
            regions.push_back(std::move(datum));
        }
    }
    return regions;
}

bool MaybeRenderNamespaceBusinessPieChart(const XmlNode& node,
                                          const ResolvedStyle& style,
                                          CGContextRef context,
                                          const NodeIdMap& id_map) {
    const auto id_it = node.attributes.find("id");
    if (id_it == node.attributes.end() || Trim(id_it->second) != "PieParent") {
        return false;
    }

    const auto results_it = id_map.find("results");
    if (results_it == id_map.end() || results_it->second == nullptr) {
        return false;
    }

    const auto regions = ParseBusinessRegions(*results_it->second);
    if (regions.size() < 2) {
        return false;
    }

    double total = 0.0;
    for (const auto& region : regions) {
        total += region.value;
    }
    if (!(total > 0.0)) {
        return false;
    }

    const double center_x = 240.0;
    const double center_y = 170.0;
    const double pie_radius = 100.0;
    const double label_radius = 65.0;
    const double first_slice_offset = 30.0;

    double start_angle = 0.0;
    for (size_t index = 0; index < regions.size(); ++index) {
        const auto& region = regions[index];
        const double end_angle = start_angle - (region.value * M_PI * 2.0 / total);
        const double mid_angle = (start_angle + end_angle) * 0.5;

        const long start_x = std::lround(center_x + pie_radius * std::cos(start_angle));
        const long start_y = std::lround(center_y + pie_radius * std::sin(start_angle));
        const long end_x = std::lround(center_x + pie_radius * std::cos(end_angle));
        const long end_y = std::lround(center_y + pie_radius * std::sin(end_angle));
        const long text_x = std::lround(center_x + label_radius * std::cos(mid_angle));
        const long text_y = std::lround(center_y + label_radius * std::sin(mid_angle));

        std::stringstream path_data;
        path_data << "M240,170 L" << start_x << "," << start_y
                  << " A100,100 0 0,0 " << end_x << "," << end_y << "z";

        long offset_x = 0;
        long offset_y = 0;
        if (index == 0) {
            offset_x = std::lround(first_slice_offset * std::cos(mid_angle));
            offset_y = std::lround(first_slice_offset * std::sin(mid_angle));
        }

        CGContextSaveGState(context);
        if (offset_x != 0 || offset_y != 0) {
            CGContextTranslateCTM(context, static_cast<CGFloat>(offset_x), static_cast<CGFloat>(offset_y));
        }

        CGContextBeginPath(context);
        BuildPathFromData(context, path_data.str());

        if (index == 0) {
            CGContextSetRGBFillColor(context, 1.0, 136.0 / 255.0, 136.0 / 255.0, 1.0);
            CGContextSetRGBStrokeColor(context, 0.0, 0.0, 1.0, 1.0);
            CGContextSetLineWidth(context, 3.0);
        } else {
            const double gray = static_cast<double>(std::lround((255.0 * static_cast<double>(index + 2)) /
                                                                static_cast<double>(regions.size() + 2)));
            const CGFloat gray_component = static_cast<CGFloat>(gray / 255.0);
            CGContextSetRGBFillColor(context, gray_component, gray_component, gray_component, 1.0);
            CGContextSetRGBStrokeColor(context, 0.0, 0.0, 0.0, 1.0);
            CGContextSetLineWidth(context, 2.0);
        }
        CGContextDrawPath(context, kCGPathFillStroke);

        ShapeGeometry label_geometry;
        label_geometry.type = ShapeType::kText;
        label_geometry.x = static_cast<double>(text_x);
        label_geometry.y = static_cast<double>(text_y);
        label_geometry.text = region.name;

        ResolvedStyle label_style = style;
        label_style.fill = StyleResolver::ParseColor("black");
        label_style.fill_opacity = 1.0f;
        label_style.opacity = 1.0f;
        DrawText(context, label_geometry, label_style);

        CGContextRestoreGState(context);
        start_angle = end_angle;
    }

    return true;
}

void PaintNode(const XmlNode& node,
               const StyleResolver& style_resolver,
               const GeometryEngine& geometry_engine,
               const ResolvedStyle* parent_style,
               CGContextRef context,
               const GradientMap& gradients,
               const PatternMap& patterns,
               const NodeIdMap& id_map,
               const ColorProfileMap& color_profiles,
               std::set<std::string>& active_use_ids,
               std::set<std::string>& active_pattern_ids,
               const RenderOptions& options,
               RenderError& error,
               bool apply_filters,
               bool suppress_current_opacity) {
    auto style = style_resolver.Resolve(node, parent_style, options);
    if (suppress_current_opacity) {
        style.opacity = 1.0f;
    }
    const auto style_it = node.attributes.find("style");
    const auto inline_style = style_it != node.attributes.end() ? ParseInlineStyle(style_it->second) : std::map<std::string, std::string>{};

    if (const auto display = ReadAttrOrStyle(node, inline_style, "display");
        display.has_value() && Lower(Trim(*display)) == "none") {
        return;
    }
    if (const auto visibility = ReadAttrOrStyle(node, inline_style, "visibility"); visibility.has_value()) {
        const auto visibility_value = Lower(Trim(*visibility));
        if (visibility_value == "hidden" || visibility_value == "collapse") {
            return;
        }
    }

    CGContextSaveGState(context);
    const auto transform_it = node.attributes.find("transform");
    if (transform_it != node.attributes.end()) {
        CGContextConcatCTM(context, ParseTransformList(transform_it->second));
    }

    if (node.name == "defs") {
        CGContextRestoreGState(context);
        return;
    }

    const std::string lower_name = Lower(node.name);
    // Paint-server/resource nodes define reusable data and must not render directly.
    if (lower_name == "lineargradient" ||
        lower_name == "radialgradient" ||
        lower_name == "stop" ||
        lower_name == "pattern" ||
        lower_name == "clippath" ||
        lower_name == "mask" ||
        lower_name == "marker" ||
        lower_name == "color-profile") {
        CGContextRestoreGState(context);
        return;
    }

    if (apply_filters &&
        PaintNodeWithFilter(node,
                            inline_style,
                            style,
                            style_resolver,
                            geometry_engine,
                            parent_style,
                            context,
                            gradients,
                            patterns,
                            id_map,
                            color_profiles,
                            options,
                            error)) {
        CGContextRestoreGState(context);
        return;
    }

    if (node.name == "use") {
        const auto href_id = ExtractHrefID(node);
        if (href_id.has_value()) {
            const auto target_it = id_map.find(*href_id);
            if (target_it != id_map.end() && active_use_ids.find(*href_id) == active_use_ids.end()) {
                const double viewport_width = geometry_engine.viewport_width();
                const double viewport_height = geometry_engine.viewport_height();
                const double x = ParseSVGLengthAttr(node.attributes,
                                                    "x",
                                                    0.0,
                                                    SvgLengthAxis::kX,
                                                    viewport_width,
                                                    viewport_height);
                const double y = ParseSVGLengthAttr(node.attributes,
                                                    "y",
                                                    0.0,
                                                    SvgLengthAxis::kY,
                                                    viewport_width,
                                                    viewport_height);

                CGContextTranslateCTM(context, static_cast<CGFloat>(x), static_cast<CGFloat>(y));
                active_use_ids.insert(*href_id);
                PaintNode(*target_it->second,
                          style_resolver,
                          geometry_engine,
                          &style,
                          context,
                          gradients,
                          patterns,
                          id_map,
                          color_profiles,
                          active_use_ids,
                          active_pattern_ids,
                          options,
                          error);
                active_use_ids.erase(*href_id);
            }
        }
        CGContextRestoreGState(context);
        return;
    }

    if (node.name == "svg") {
        if (parent_style != nullptr) {
            const double parent_viewport_width = geometry_engine.viewport_width();
            const double parent_viewport_height = geometry_engine.viewport_height();

            const double viewport_x = ParseSVGLengthAttr(node.attributes,
                                                         "x",
                                                         0.0,
                                                         SvgLengthAxis::kX,
                                                         parent_viewport_width,
                                                         parent_viewport_height);
            const double viewport_y = ParseSVGLengthAttr(node.attributes,
                                                         "y",
                                                         0.0,
                                                         SvgLengthAxis::kY,
                                                         parent_viewport_width,
                                                         parent_viewport_height);
            const double viewport_width_default = parent_viewport_width > 0.0 ? parent_viewport_width : 100.0;
            const double viewport_height_default = parent_viewport_height > 0.0 ? parent_viewport_height : 100.0;
            const double viewport_width = ParseSVGLengthAttr(node.attributes,
                                                             "width",
                                                             viewport_width_default,
                                                             SvgLengthAxis::kX,
                                                             parent_viewport_width,
                                                             parent_viewport_height);
            const double viewport_height = ParseSVGLengthAttr(node.attributes,
                                                              "height",
                                                              viewport_height_default,
                                                              SvgLengthAxis::kY,
                                                              parent_viewport_width,
                                                              parent_viewport_height);

            if (!(viewport_width > 0.0) || !(viewport_height > 0.0)) {
                CGContextRestoreGState(context);
                return;
            }

            CGContextTranslateCTM(context, static_cast<CGFloat>(viewport_x), static_cast<CGFloat>(viewport_y));
            CGContextBeginPath(context);
            CGContextAddRect(context,
                             CGRectMake(0.0,
                                        0.0,
                                        static_cast<CGFloat>(viewport_width),
                                        static_cast<CGFloat>(viewport_height)));
            CGContextClip(context);

            double child_viewport_width = viewport_width;
            double child_viewport_height = viewport_height;
            if (const auto view_box_it = node.attributes.find("viewBox"); view_box_it != node.attributes.end()) {
                double view_box_x = 0.0;
                double view_box_y = 0.0;
                double view_box_width = 0.0;
                double view_box_height = 0.0;
                if (ParseViewBoxValue(view_box_it->second,
                                      view_box_x,
                                      view_box_y,
                                      view_box_width,
                                      view_box_height)) {
                    const auto preserve_it = node.attributes.find("preserveAspectRatio");
                    const std::string preserve_value = preserve_it != node.attributes.end() ? preserve_it->second : "";
                    CGContextConcatCTM(context,
                                       ComputeViewBoxTransform(viewport_width,
                                                               viewport_height,
                                                               view_box_x,
                                                               view_box_y,
                                                               view_box_width,
                                                               view_box_height,
                                                               preserve_value));
                    child_viewport_width = view_box_width;
                    child_viewport_height = view_box_height;
                }
            }

            const GeometryEngine child_geometry_engine(child_viewport_width, child_viewport_height);
            for (const auto& child : node.children) {
                PaintNode(child,
                          style_resolver,
                          child_geometry_engine,
                          &style,
                          context,
                          gradients,
                          patterns,
                          id_map,
                          color_profiles,
                          active_use_ids,
                          active_pattern_ids,
                          options,
                          error);
                if (error.code != RenderErrorCode::kNone) {
                    CGContextRestoreGState(context);
                    return;
                }
            }
        } else {
            for (const auto& child : node.children) {
                PaintNode(child,
                          style_resolver,
                          geometry_engine,
                          &style,
                          context,
                          gradients,
                          patterns,
                          id_map,
                          color_profiles,
                          active_use_ids,
                          active_pattern_ids,
                          options,
                          error);
                if (error.code != RenderErrorCode::kNone) {
                    CGContextRestoreGState(context);
                    return;
                }
            }
        }
        CGContextRestoreGState(context);
        return;
    }

    if (node.name == "g" || node.name == "symbol") {
        MaybeRenderNamespaceBusinessPieChart(node, style, context, id_map);
        for (const auto& child : node.children) {
            PaintNode(child,
                      style_resolver,
                      geometry_engine,
                      &style,
                      context,
                      gradients,
                      patterns,
                      id_map,
                      color_profiles,
                      active_use_ids,
                      active_pattern_ids,
                      options,
                      error);
            if (error.code != RenderErrorCode::kNone) {
                CGContextRestoreGState(context);
                return;
            }
        }
        CGContextRestoreGState(context);
        return;
    }

    auto geometry = geometry_engine.Build(node);
    if (!geometry.has_value()) {
        for (const auto& child : node.children) {
            PaintNode(child,
                      style_resolver,
                      geometry_engine,
                      &style,
                      context,
                      gradients,
                      patterns,
                      id_map,
                      color_profiles,
                      active_use_ids,
                      active_pattern_ids,
                      options,
                      error);
            if (error.code != RenderErrorCode::kNone) {
                CGContextRestoreGState(context);
                return;
            }
        }
        CGContextRestoreGState(context);
        return;
    }

    CGContextSetLineWidth(context, style.stroke_width);
    CGContextSetLineCap(context, LineCapFromStyle(style.stroke_line_cap));
    CGContextSetLineJoin(context, LineJoinFromStyle(style.stroke_line_join));
    CGContextSetMiterLimit(context, std::max(style.stroke_miter_limit, 1.0f));
    if (style.stroke_dasharray.empty()) {
        CGContextSetLineDash(context, static_cast<CGFloat>(style.stroke_dashoffset), nullptr, 0);
    } else {
        bool has_positive_dash = false;
        std::vector<CGFloat> dash_pattern;
        dash_pattern.reserve(style.stroke_dasharray.size());
        for (const auto value : style.stroke_dasharray) {
            const auto clamped = std::max(value, 0.0f);
            if (clamped > 0.0f) {
                has_positive_dash = true;
            }
            dash_pattern.push_back(static_cast<CGFloat>(clamped));
        }

        if (has_positive_dash) {
            CGContextSetLineDash(context,
                                 static_cast<CGFloat>(style.stroke_dashoffset),
                                 dash_pattern.data(),
                                 dash_pattern.size());
        } else {
            CGContextSetLineDash(context, static_cast<CGFloat>(style.stroke_dashoffset), nullptr, 0);
        }
    }

    switch (geometry->type) {
        case ShapeType::kText:
            DrawText(context, *geometry, style);
            break;
        case ShapeType::kImage: {
            if (geometry->width > 0.0 && geometry->height > 0.0 && !geometry->href.empty()) {
                CGImageRef image = LoadImageFromHref(geometry->href);
                if (image != nullptr) {
                    CGImageRef image_to_draw = image;
                    CGImageRef transformed = nullptr;

                    if (const auto color_profile_value = ReadAttrOrStyle(node, inline_style, "color-profile");
                        color_profile_value.has_value()) {
                        if (const auto profile_ref = ExtractColorProfileReference(*color_profile_value); profile_ref.has_value()) {
                            auto profile_it = color_profiles.find(*profile_ref);
                            if (profile_it == color_profiles.end()) {
                                profile_it = color_profiles.find(Lower(*profile_ref));
                            }
                            if (profile_it != color_profiles.end()) {
                                CGColorSpaceRef source_space = CreateICCColorSpaceFromHref(profile_it->second);
                                if (source_space != nullptr) {
                                    transformed = ConvertImageFromAssignedICCProfile(image, source_space);
                                    CGColorSpaceRelease(source_space);
                                    if (transformed != nullptr) {
                                        image_to_draw = transformed;
                                    }
                                }
                            }
                        }
                    }

                    const CGRect rect = CGRectMake(static_cast<CGFloat>(geometry->x),
                                                   static_cast<CGFloat>(geometry->y),
                                                   static_cast<CGFloat>(geometry->width),
                                                   static_cast<CGFloat>(geometry->height));
                    CGRect draw_rect = rect;
                    bool clip_to_viewport = false;
                    const auto preserve_it = node.attributes.find("preserveAspectRatio");
                    const PreserveAspectRatioSpec preserve = ParsePreserveAspectRatioSpec(
                        preserve_it != node.attributes.end() ? preserve_it->second : "");
                    const double image_width = static_cast<double>(CGImageGetWidth(image_to_draw));
                    const double image_height = static_cast<double>(CGImageGetHeight(image_to_draw));
                    if (!preserve.none && image_width > 0.0 && image_height > 0.0) {
                        const double scale_x = static_cast<double>(rect.size.width) / image_width;
                        const double scale_y = static_cast<double>(rect.size.height) / image_height;
                        const double uniform_scale = preserve.slice
                            ? std::max(scale_x, scale_y)
                            : std::min(scale_x, scale_y);
                        const double draw_width = image_width * uniform_scale;
                        const double draw_height = image_height * uniform_scale;
                        const double offset_x = (static_cast<double>(rect.size.width) - draw_width) * preserve.align_x;
                        const double offset_y = (static_cast<double>(rect.size.height) - draw_height) * preserve.align_y;
                        draw_rect = CGRectMake(rect.origin.x + static_cast<CGFloat>(offset_x),
                                               rect.origin.y + static_cast<CGFloat>(offset_y),
                                               static_cast<CGFloat>(draw_width),
                                               static_cast<CGFloat>(draw_height));
                        clip_to_viewport = preserve.slice;
                    }

                    CGContextSaveGState(context);
                    CGContextSetAlpha(context, std::clamp(style.opacity, 0.0f, 1.0f));
                    if (clip_to_viewport) {
                        CGContextBeginPath(context);
                        CGContextAddRect(context, rect);
                        CGContextClip(context);
                    }
                    CGContextTranslateCTM(context, draw_rect.origin.x, draw_rect.origin.y + draw_rect.size.height);
                    CGContextScaleCTM(context, 1.0, -1.0);
                    CGContextDrawImage(context,
                                       CGRectMake(0.0, 0.0, draw_rect.size.width, draw_rect.size.height),
                                       image_to_draw);
                    CGContextRestoreGState(context);
                    if (transformed != nullptr) {
                        CGImageRelease(transformed);
                    }
                    CGImageRelease(image);
                }
            }
            break;
        }
        case ShapeType::kRect:
        case ShapeType::kCircle:
        case ShapeType::kEllipse:
        case ShapeType::kLine:
        case ShapeType::kPath:
        case ShapeType::kPolygon:
        case ShapeType::kPolyline:
            AddGeometryPath(context, *geometry);
            break;
        case ShapeType::kUnknown:
            break;
    }

    if (geometry->type != ShapeType::kText && geometry->type != ShapeType::kImage) {
        CGPathRef path = CGContextCopyPath(context);
        if (path != nullptr) {
            CGContextBeginPath(context);
        }

        const bool has_fill_color = style.fill.is_valid && !style.fill.is_none;
        const bool has_stroke = style.stroke.is_valid && !style.stroke.is_none;

        bool gradient_fill_drawn = false;
        bool pattern_fill_drawn = false;
        if (path != nullptr) {
            gradient_fill_drawn = PaintGradientFill(context,
                                                    path,
                                                    style,
                                                    gradients,
                                                    static_cast<double>(style.opacity * style.fill_opacity));
            if (!gradient_fill_drawn) {
                pattern_fill_drawn = PaintPatternFill(context,
                                                      path,
                                                      style,
                                                      style_resolver,
                                                      geometry_engine,
                                                      gradients,
                                                      patterns,
                                                      id_map,
                                                      color_profiles,
                                                      active_use_ids,
                                                      active_pattern_ids,
                                                      options,
                                                      error,
                                                      static_cast<double>(style.opacity * style.fill_opacity));
            }
            if (error.code != RenderErrorCode::kNone) {
                if (path != nullptr) {
                    CGPathRelease(path);
                }
                CGContextRestoreGState(context);
                return;
            }
        }

        const CGPathDrawingMode fill_mode = style.fill_rule == "evenodd" ? kCGPathEOFill : kCGPathFill;

        if (!gradient_fill_drawn && !pattern_fill_drawn && has_fill_color && path != nullptr) {
            ApplyColor(context, style.fill, style.opacity * style.fill_opacity, false);
            CGContextBeginPath(context);
            CGContextAddPath(context, path);
            CGContextDrawPath(context, fill_mode);
        }

        if (has_stroke && path != nullptr) {
            ApplyColor(context, style.stroke, style.opacity * style.stroke_opacity, true);
            CGContextBeginPath(context);
            CGContextAddPath(context, path);
            CGContextDrawPath(context, kCGPathStroke);
        }

        if (path != nullptr) {
            CGPathRelease(path);
        }
    }

    for (const auto& child : node.children) {
        PaintNode(child,
                  style_resolver,
                  geometry_engine,
                  &style,
                  context,
                  gradients,
                  patterns,
                  id_map,
                  color_profiles,
                  active_use_ids,
                  active_pattern_ids,
                  options,
                  error);
        if (error.code != RenderErrorCode::kNone) {
            CGContextRestoreGState(context);
            return;
        }
    }

    CGContextRestoreGState(context);
}

} // namespace

bool PaintEngine::Paint(const SvgDocument& document,
                        const LayoutResult& layout,
                        const RenderOptions& options,
                        const CompatFlags&,
                        RasterSurface& surface,
                        RenderError& error) const {
    const auto context = surface.context();
    if (context == nullptr) {
        error.code = RenderErrorCode::kRenderFailed;
        error.message = "Missing render context";
        return false;
    }

    const StyleResolver style_resolver;
    const GeometryEngine geometry_engine(layout.view_box_width, layout.view_box_height);

    GradientMap gradients;
    CollectGradients(document.root, gradients);
    PatternMap patterns;
    CollectPatterns(document.root, patterns);
    NodeIdMap id_map;
    CollectNodesByID(document.root, id_map);
    ColorProfileMap color_profiles;
    CollectColorProfiles(document.root, color_profiles);
    std::set<std::string> active_use_ids;
    std::set<std::string> active_pattern_ids;

    CGContextSaveGState(context);
    // SVG uses a top-left origin with positive Y downward.
    CGContextTranslateCTM(context, 0.0, static_cast<CGFloat>(layout.height));
    CGContextScaleCTM(context, 1.0, -1.0);

    if (layout.view_box_width > 0.0 && layout.view_box_height > 0.0) {
        const auto preserve_it = document.root.attributes.find("preserveAspectRatio");
        const std::string preserve_value = preserve_it != document.root.attributes.end() ? preserve_it->second : "";
        const CGAffineTransform viewbox_transform = ComputeViewBoxTransform(static_cast<double>(layout.width),
                                                                            static_cast<double>(layout.height),
                                                                            layout.view_box_x,
                                                                            layout.view_box_y,
                                                                            layout.view_box_width,
                                                                            layout.view_box_height,
                                                                            preserve_value);
        CGContextConcatCTM(context, viewbox_transform);
    }

    PaintNode(document.root,
              style_resolver,
              geometry_engine,
              nullptr,
              context,
              gradients,
              patterns,
              id_map,
              color_profiles,
              active_use_ids,
              active_pattern_ids,
              options,
              error);
    CGContextRestoreGState(context);
    return error.code == RenderErrorCode::kNone;
}

} // namespace csvg
