#ifndef CHROMIUM_SVG_C_BRIDGE_H
#define CHROMIUM_SVG_C_BRIDGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct csvg_renderer csvg_renderer_t;

typedef enum csvg_error_code {
    CSVG_ERROR_NONE = 0,
    CSVG_ERROR_INVALID_DOCUMENT = 1,
    CSVG_ERROR_UNSUPPORTED_FEATURE = 2,
    CSVG_ERROR_EXTERNAL_RESOURCE_BLOCKED = 3,
    CSVG_ERROR_EXTERNAL_RESOURCE_FAILED = 4,
    CSVG_ERROR_RENDER_FAILED = 5,
} csvg_error_code_t;

typedef enum csvg_external_resource_purpose {
    CSVG_RESOURCE_IMAGE = 0,
    CSVG_RESOURCE_STYLESHEET = 1,
    CSVG_RESOURCE_FONT = 2,
    CSVG_RESOURCE_OTHER = 3,
} csvg_external_resource_purpose_t;

typedef struct csvg_external_resource_request {
    const char* url;
    csvg_external_resource_purpose_t purpose;
} csvg_external_resource_request_t;

typedef int32_t (*csvg_external_resource_loader_t)(void* context,
                                                   const csvg_external_resource_request_t* request,
                                                   uint8_t** out_data,
                                                   size_t* out_size,
                                                   char** out_error_message);

typedef struct csvg_render_options {
    int32_t viewport_width;
    int32_t viewport_height;
    float scale;

    float background_red;
    float background_green;
    float background_blue;
    float background_alpha;

    const char* default_font_family;
    float default_font_size;
    bool enable_external_resources;
} csvg_render_options_t;

typedef struct csvg_render_result {
    int32_t width;
    int32_t height;
    uint8_t* rgba;
    size_t rgba_size;

    csvg_error_code_t error_code;
    char* error_message;
} csvg_render_result_t;

csvg_renderer_t* csvg_renderer_create(void);
void csvg_renderer_destroy(csvg_renderer_t* renderer);

void csvg_renderer_set_external_resource_loader(csvg_renderer_t* renderer,
                                                csvg_external_resource_loader_t loader,
                                                void* context);

void csvg_render_options_init_default(csvg_render_options_t* out_options);

int32_t csvg_renderer_render(csvg_renderer_t* renderer,
                             const uint8_t* svg_bytes,
                             size_t svg_size,
                             const csvg_render_options_t* options,
                             csvg_render_result_t* out_result);

void csvg_render_result_free(csvg_render_result_t* result);
void csvg_free_owned_memory(void* memory);

#ifdef __cplusplus
}
#endif

#endif
