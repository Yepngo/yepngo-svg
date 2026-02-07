#ifndef CHROMIUM_SVG_CORE_COMPAT_FLAGS_HPP
#define CHROMIUM_SVG_CORE_COMPAT_FLAGS_HPP

namespace csvg {

struct CompatFlags {
    bool strict_mode = true;
    bool deterministic_rendering = true;
    bool allow_unsupported_filter_fallback = false;
};

} // namespace csvg

#endif
