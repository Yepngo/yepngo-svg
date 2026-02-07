#include "YepSVGCore/GeometryEngine.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <string>

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

enum class LengthAxis {
    kX,
    kY,
    kDiagonal,
};

double NormalizeDiagonal(double width, double height) {
    if (width <= 0.0 || height <= 0.0) {
        return 100.0;
    }
    return std::sqrt((width * width + height * height) / 2.0);
}

double PercentBasis(LengthAxis axis, double viewport_width, double viewport_height) {
    switch (axis) {
        case LengthAxis::kX:
            return viewport_width > 0.0 ? viewport_width : 100.0;
        case LengthAxis::kY:
            return viewport_height > 0.0 ? viewport_height : 100.0;
        case LengthAxis::kDiagonal:
            return NormalizeDiagonal(viewport_width, viewport_height);
    }
    return 100.0;
}

double ConvertAbsoluteLength(double value, const std::string& unit) {
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

double ParseLength(const std::string& raw,
                   double fallback,
                   LengthAxis axis,
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
        const double basis = PercentBasis(axis, viewport_width, viewport_height);
        return (parsed / 100.0) * basis;
    }

    return ConvertAbsoluteLength(parsed, suffix);
}

double ParseLengthAttr(const std::map<std::string, std::string>& attributes,
                       const std::string& key,
                       double fallback,
                       LengthAxis axis,
                       double viewport_width,
                       double viewport_height) {
    const auto it = attributes.find(key);
    if (it == attributes.end()) {
        return fallback;
    }
    return ParseLength(it->second, fallback, axis, viewport_width, viewport_height);
}

} // namespace

GeometryEngine::GeometryEngine(double viewport_width, double viewport_height)
    : viewport_width_(viewport_width), viewport_height_(viewport_height) {}

double GeometryEngine::viewport_width() const {
    return viewport_width_;
}

double GeometryEngine::viewport_height() const {
    return viewport_height_;
}

std::vector<Point> GeometryEngine::ParsePointList(const std::string& value) {
    std::vector<Point> points;
    std::vector<double> values;
    size_t i = 0;

    while (i < value.size()) {
        while (i < value.size() && (std::isspace(static_cast<unsigned char>(value[i])) || value[i] == ',')) {
            ++i;
        }
        if (i >= value.size()) {
            break;
        }

        const char* start = value.c_str() + i;
        char* end_ptr = nullptr;
        const double parsed = std::strtod(start, &end_ptr);
        if (end_ptr == start) {
            ++i;
            continue;
        }
        values.push_back(parsed);
        i = static_cast<size_t>(end_ptr - value.c_str());
    }

    for (size_t index = 0; index + 1 < values.size(); index += 2) {
        points.push_back({values[index], values[index + 1]});
    }
    return points;
}

std::optional<ShapeGeometry> GeometryEngine::Build(const XmlNode& node) const {
    ShapeGeometry geometry;

    if (node.name == "rect") {
        geometry.type = ShapeType::kRect;
        geometry.x = ParseLengthAttr(node.attributes, "x", 0.0, LengthAxis::kX, viewport_width_, viewport_height_);
        geometry.y = ParseLengthAttr(node.attributes, "y", 0.0, LengthAxis::kY, viewport_width_, viewport_height_);
        geometry.width = ParseLengthAttr(node.attributes, "width", 0.0, LengthAxis::kX, viewport_width_, viewport_height_);
        geometry.height = ParseLengthAttr(node.attributes, "height", 0.0, LengthAxis::kY, viewport_width_, viewport_height_);
        geometry.rx = ParseLengthAttr(node.attributes, "rx", 0.0, LengthAxis::kX, viewport_width_, viewport_height_);
        geometry.ry = ParseLengthAttr(node.attributes, "ry", 0.0, LengthAxis::kY, viewport_width_, viewport_height_);
        return geometry;
    }

    if (node.name == "circle") {
        geometry.type = ShapeType::kCircle;
        geometry.x = ParseLengthAttr(node.attributes, "cx", 0.0, LengthAxis::kX, viewport_width_, viewport_height_);
        geometry.y = ParseLengthAttr(node.attributes, "cy", 0.0, LengthAxis::kY, viewport_width_, viewport_height_);
        geometry.rx = ParseLengthAttr(node.attributes, "r", 0.0, LengthAxis::kDiagonal, viewport_width_, viewport_height_);
        return geometry;
    }

    if (node.name == "ellipse") {
        geometry.type = ShapeType::kEllipse;
        geometry.x = ParseLengthAttr(node.attributes, "cx", 0.0, LengthAxis::kX, viewport_width_, viewport_height_);
        geometry.y = ParseLengthAttr(node.attributes, "cy", 0.0, LengthAxis::kY, viewport_width_, viewport_height_);
        geometry.rx = ParseLengthAttr(node.attributes, "rx", 0.0, LengthAxis::kX, viewport_width_, viewport_height_);
        geometry.ry = ParseLengthAttr(node.attributes, "ry", 0.0, LengthAxis::kY, viewport_width_, viewport_height_);
        return geometry;
    }

    if (node.name == "line") {
        geometry.type = ShapeType::kLine;
        geometry.points.push_back({
            ParseLengthAttr(node.attributes, "x1", 0.0, LengthAxis::kX, viewport_width_, viewport_height_),
            ParseLengthAttr(node.attributes, "y1", 0.0, LengthAxis::kY, viewport_width_, viewport_height_),
        });
        geometry.points.push_back({
            ParseLengthAttr(node.attributes, "x2", 0.0, LengthAxis::kX, viewport_width_, viewport_height_),
            ParseLengthAttr(node.attributes, "y2", 0.0, LengthAxis::kY, viewport_width_, viewport_height_),
        });
        return geometry;
    }

    if (node.name == "polygon") {
        geometry.type = ShapeType::kPolygon;
        const auto it = node.attributes.find("points");
        if (it != node.attributes.end()) {
            geometry.points = ParsePointList(it->second);
        }
        return geometry;
    }

    if (node.name == "polyline") {
        geometry.type = ShapeType::kPolyline;
        const auto it = node.attributes.find("points");
        if (it != node.attributes.end()) {
            geometry.points = ParsePointList(it->second);
        }
        return geometry;
    }

    if (node.name == "path") {
        geometry.type = ShapeType::kPath;
        const auto it = node.attributes.find("d");
        if (it != node.attributes.end()) {
            geometry.path_data = it->second;
        }
        return geometry;
    }

    if (node.name == "text") {
        geometry.type = ShapeType::kText;
        geometry.x = ParseLengthAttr(node.attributes, "x", 0.0, LengthAxis::kX, viewport_width_, viewport_height_);
        geometry.y = ParseLengthAttr(node.attributes, "y", 0.0, LengthAxis::kY, viewport_width_, viewport_height_);
        geometry.text = node.text;
        return geometry;
    }

    if (node.name == "image") {
        geometry.type = ShapeType::kImage;
        geometry.x = ParseLengthAttr(node.attributes, "x", 0.0, LengthAxis::kX, viewport_width_, viewport_height_);
        geometry.y = ParseLengthAttr(node.attributes, "y", 0.0, LengthAxis::kY, viewport_width_, viewport_height_);
        geometry.width = ParseLengthAttr(node.attributes, "width", 0.0, LengthAxis::kX, viewport_width_, viewport_height_);
        geometry.height = ParseLengthAttr(node.attributes, "height", 0.0, LengthAxis::kY, viewport_width_, viewport_height_);
        const auto href_it = node.attributes.find("href");
        if (href_it != node.attributes.end()) {
            geometry.href = href_it->second;
        } else {
            const auto xlink_href_it = node.attributes.find("xlink:href");
            if (xlink_href_it != node.attributes.end()) {
                geometry.href = xlink_href_it->second;
            }
        }
        return geometry;
    }

    return std::nullopt;
}

} // namespace csvg
