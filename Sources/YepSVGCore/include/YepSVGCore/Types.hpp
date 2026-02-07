#ifndef CHROMIUM_SVG_CORE_TYPES_HPP
#define CHROMIUM_SVG_CORE_TYPES_HPP

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace csvg {

enum class RenderErrorCode : int32_t {
    kNone = 0,
    kInvalidDocument = 1,
    kUnsupportedFeature = 2,
    kExternalResourceBlocked = 3,
    kExternalResourceFailed = 4,
    kRenderFailed = 5,
};

struct RenderError {
    RenderErrorCode code = RenderErrorCode::kNone;
    std::string message;
};

struct RenderOptions {
    int32_t viewport_width = 0;
    int32_t viewport_height = 0;
    float scale = 1.0f;
    float background_red = 0.0f;
    float background_green = 0.0f;
    float background_blue = 0.0f;
    float background_alpha = 0.0f;
    std::string default_font_family = "Helvetica";
    float default_font_size = 16.0f;
    bool enable_external_resources = false;
};

struct ImageBuffer {
    int32_t width = 0;
    int32_t height = 0;
    std::vector<uint8_t> rgba;
};

struct Point {
    double x = 0.0;
    double y = 0.0;
};

struct Color {
    bool is_none = false;
    bool is_valid = false;
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float a = 1.0f;
};

struct XmlNode {
    std::string name;
    std::map<std::string, std::string> attributes;
    std::vector<XmlNode> children;
    std::string text;
};

struct SvgDocument {
    XmlNode root;
};

} // namespace csvg

#endif
