#include "YepSVGCore/Engine.hpp"

#include "YepSVGCore/FilterGraph.hpp"
#include "YepSVGCore/LayoutEngine.hpp"
#include "YepSVGCore/PaintEngine.hpp"
#include "YepSVGCore/ResourceResolver.hpp"
#include "YepSVGCore/RasterBackendCG.hpp"
#include "YepSVGCore/SvgDom.hpp"
#include "YepSVGCore/XmlParser.hpp"

namespace csvg {

Engine::Engine() = default;

bool Engine::Render(const std::string& svg_text,
                    const RenderOptions& options,
                    ImageBuffer& out_image,
                    RenderError& out_error) const {
    out_error = {};
    out_image = {};

    XmlParser xml_parser;
    SvgDom dom_builder;
    LayoutEngine layout_engine;
    FilterGraph filter_graph;
    ResourceResolver resource_resolver;
    PaintEngine paint_engine;

    auto xml_root = xml_parser.Parse(svg_text, out_error);
    if (!xml_root.has_value()) {
        return false;
    }

    auto document = dom_builder.Build(*xml_root, out_error);
    if (!document.has_value()) {
        return false;
    }

    const auto urls = resource_resolver.CollectExternalURLs(document->root);
    if (!resource_resolver.ValidatePolicy(urls, options, out_error)) {
        return false;
    }

    if (!filter_graph.ValidateFilterSupport(document->root, flags_, out_error)) {
        return false;
    }

    auto layout = layout_engine.Compute(*document, options, out_error);
    if (!layout.has_value()) {
        return false;
    }

    Color background;
    background.is_none = false;
    background.is_valid = options.background_alpha > 0.0f;
    background.r = options.background_red;
    background.g = options.background_green;
    background.b = options.background_blue;
    background.a = options.background_alpha;

    RasterSurface surface(layout->width, layout->height, background);
    if (!paint_engine.Paint(*document, *layout, options, flags_, surface, out_error)) {
        return false;
    }

    if (!surface.Extract(out_image, out_error)) {
        return false;
    }

    return true;
}

} // namespace csvg
