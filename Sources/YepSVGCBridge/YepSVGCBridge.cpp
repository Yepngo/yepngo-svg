#include "YepSVGCBridge/chromium_svg_c_bridge.h"

#include <cstring>
#include <memory>
#include <new>
#include <string>

#include "YepSVGCore/Engine.hpp"

struct csvg_renderer {
    csvg::Engine engine;
    csvg_external_resource_loader_t loader = nullptr;
    void* loader_context = nullptr;
};

namespace {

csvg_error_code_t ToBridgeCode(csvg::RenderErrorCode code) {
    switch (code) {
        case csvg::RenderErrorCode::kNone:
            return CSVG_ERROR_NONE;
        case csvg::RenderErrorCode::kInvalidDocument:
            return CSVG_ERROR_INVALID_DOCUMENT;
        case csvg::RenderErrorCode::kUnsupportedFeature:
            return CSVG_ERROR_UNSUPPORTED_FEATURE;
        case csvg::RenderErrorCode::kExternalResourceBlocked:
            return CSVG_ERROR_EXTERNAL_RESOURCE_BLOCKED;
        case csvg::RenderErrorCode::kExternalResourceFailed:
            return CSVG_ERROR_EXTERNAL_RESOURCE_FAILED;
        case csvg::RenderErrorCode::kRenderFailed:
        default:
            return CSVG_ERROR_RENDER_FAILED;
    }
}

char* CopyCString(const std::string& value) {
    char* out = static_cast<char*>(std::malloc(value.size() + 1));
    if (out == nullptr) {
        return nullptr;
    }
    std::memcpy(out, value.data(), value.size());
    out[value.size()] = '\0';
    return out;
}

csvg::RenderOptions ToCoreOptions(const csvg_render_options_t* options) {
    csvg::RenderOptions core;
    if (options == nullptr) {
        return core;
    }

    core.viewport_width = options->viewport_width;
    core.viewport_height = options->viewport_height;
    core.scale = options->scale <= 0.0f ? 1.0f : options->scale;

    core.background_red = options->background_red;
    core.background_green = options->background_green;
    core.background_blue = options->background_blue;
    core.background_alpha = options->background_alpha;

    if (options->default_font_family != nullptr && options->default_font_family[0] != '\0') {
        core.default_font_family = options->default_font_family;
    }
    core.default_font_size = options->default_font_size > 0.0f ? options->default_font_size : 16.0f;
    core.enable_external_resources = options->enable_external_resources;
    return core;
}

} // namespace

csvg_renderer_t* csvg_renderer_create(void) {
    return new (std::nothrow) csvg_renderer_t();
}

void csvg_renderer_destroy(csvg_renderer_t* renderer) {
    delete renderer;
}

void csvg_renderer_set_external_resource_loader(csvg_renderer_t* renderer,
                                                csvg_external_resource_loader_t loader,
                                                void* context) {
    if (renderer == nullptr) {
        return;
    }
    renderer->loader = loader;
    renderer->loader_context = context;
}

void csvg_render_options_init_default(csvg_render_options_t* out_options) {
    if (out_options == nullptr) {
        return;
    }

    out_options->viewport_width = 0;
    out_options->viewport_height = 0;
    out_options->scale = 1.0f;

    out_options->background_red = 0.0f;
    out_options->background_green = 0.0f;
    out_options->background_blue = 0.0f;
    out_options->background_alpha = 0.0f;

    out_options->default_font_family = "Helvetica";
    out_options->default_font_size = 16.0f;
    out_options->enable_external_resources = false;
}

int32_t csvg_renderer_render(csvg_renderer_t* renderer,
                             const uint8_t* svg_bytes,
                             size_t svg_size,
                             const csvg_render_options_t* options,
                             csvg_render_result_t* out_result) {
    if (renderer == nullptr || svg_bytes == nullptr || svg_size == 0 || out_result == nullptr) {
        return 0;
    }

    out_result->width = 0;
    out_result->height = 0;
    out_result->rgba = nullptr;
    out_result->rgba_size = 0;
    out_result->error_code = CSVG_ERROR_NONE;
    out_result->error_message = nullptr;

    const std::string svg_text(reinterpret_cast<const char*>(svg_bytes), svg_size);

    csvg::ImageBuffer image;
    csvg::RenderError error;
    if (!renderer->engine.Render(svg_text, ToCoreOptions(options), image, error)) {
        out_result->error_code = ToBridgeCode(error.code);
        out_result->error_message = CopyCString(error.message.empty() ? "Unknown render failure" : error.message);
        return 0;
    }

    out_result->width = image.width;
    out_result->height = image.height;
    out_result->rgba_size = image.rgba.size();
    out_result->rgba = static_cast<uint8_t*>(std::malloc(out_result->rgba_size));
    if (out_result->rgba == nullptr) {
        out_result->error_code = CSVG_ERROR_RENDER_FAILED;
        out_result->error_message = CopyCString("Failed to allocate output pixel buffer");
        return 0;
    }

    std::memcpy(out_result->rgba, image.rgba.data(), out_result->rgba_size);
    return 1;
}

void csvg_render_result_free(csvg_render_result_t* result) {
    if (result == nullptr) {
        return;
    }

    if (result->rgba != nullptr) {
        std::free(result->rgba);
        result->rgba = nullptr;
    }
    if (result->error_message != nullptr) {
        std::free(result->error_message);
        result->error_message = nullptr;
    }

    result->width = 0;
    result->height = 0;
    result->rgba_size = 0;
    result->error_code = CSVG_ERROR_NONE;
}

void csvg_free_owned_memory(void* memory) {
    if (memory != nullptr) {
        std::free(memory);
    }
}
