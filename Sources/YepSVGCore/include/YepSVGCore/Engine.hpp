#ifndef CHROMIUM_SVG_CORE_ENGINE_HPP
#define CHROMIUM_SVG_CORE_ENGINE_HPP

#include <optional>
#include <string>

#include "YepSVGCore/CompatFlags.hpp"
#include "YepSVGCore/Types.hpp"

namespace csvg {

class Engine {
public:
    Engine();

    bool Render(const std::string& svg_text,
                const RenderOptions& options,
                ImageBuffer& out_image,
                RenderError& out_error) const;

private:
    CompatFlags flags_;
};

} // namespace csvg

#endif
