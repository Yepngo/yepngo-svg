#ifndef CHROMIUM_SVG_CORE_GEOMETRY_ENGINE_HPP
#define CHROMIUM_SVG_CORE_GEOMETRY_ENGINE_HPP

#include <optional>
#include <string>
#include <vector>

#include "YepSVGCore/Types.hpp"

namespace csvg {

enum class ShapeType {
    kUnknown,
    kRect,
    kCircle,
    kEllipse,
    kLine,
    kPath,
    kPolygon,
    kPolyline,
    kText,
};

struct ShapeGeometry {
    ShapeType type = ShapeType::kUnknown;
    double x = 0.0;
    double y = 0.0;
    double width = 0.0;
    double height = 0.0;
    double rx = 0.0;
    double ry = 0.0;
    std::vector<Point> points;
    std::string path_data;
    std::string text;
};

class GeometryEngine {
public:
    std::optional<ShapeGeometry> Build(const XmlNode& node) const;
    static std::vector<Point> ParsePointList(const std::string& value);
};

} // namespace csvg

#endif
