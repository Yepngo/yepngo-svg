#include "YepSVGCore/GeometryEngine.hpp"

#include <cctype>
#include <cstdlib>

namespace csvg {
namespace {

double ParseNumber(const std::map<std::string, std::string>& attributes, const std::string& key, double fallback = 0.0) {
    const auto it = attributes.find(key);
    if (it == attributes.end()) {
        return fallback;
    }
    char* end_ptr = nullptr;
    const double value = std::strtod(it->second.c_str(), &end_ptr);
    if (end_ptr == it->second.c_str()) {
        return fallback;
    }
    return value;
}

} // namespace

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
        geometry.x = ParseNumber(node.attributes, "x");
        geometry.y = ParseNumber(node.attributes, "y");
        geometry.width = ParseNumber(node.attributes, "width");
        geometry.height = ParseNumber(node.attributes, "height");
        geometry.rx = ParseNumber(node.attributes, "rx");
        geometry.ry = ParseNumber(node.attributes, "ry");
        return geometry;
    }

    if (node.name == "circle") {
        geometry.type = ShapeType::kCircle;
        geometry.x = ParseNumber(node.attributes, "cx");
        geometry.y = ParseNumber(node.attributes, "cy");
        geometry.rx = ParseNumber(node.attributes, "r");
        return geometry;
    }

    if (node.name == "ellipse") {
        geometry.type = ShapeType::kEllipse;
        geometry.x = ParseNumber(node.attributes, "cx");
        geometry.y = ParseNumber(node.attributes, "cy");
        geometry.rx = ParseNumber(node.attributes, "rx");
        geometry.ry = ParseNumber(node.attributes, "ry");
        return geometry;
    }

    if (node.name == "line") {
        geometry.type = ShapeType::kLine;
        geometry.points.push_back({ParseNumber(node.attributes, "x1"), ParseNumber(node.attributes, "y1")});
        geometry.points.push_back({ParseNumber(node.attributes, "x2"), ParseNumber(node.attributes, "y2")});
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
        geometry.x = ParseNumber(node.attributes, "x");
        geometry.y = ParseNumber(node.attributes, "y");
        geometry.text = node.text;
        return geometry;
    }

    return std::nullopt;
}

} // namespace csvg
