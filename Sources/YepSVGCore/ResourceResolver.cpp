#include "YepSVGCore/ResourceResolver.hpp"

namespace csvg {
namespace {

void CollectURLsRecursive(const XmlNode& node, std::vector<std::string>& urls) {
    const auto href_it = node.attributes.find("href");
    if (href_it != node.attributes.end()) {
        urls.push_back(href_it->second);
    }
    const auto xlink_it = node.attributes.find("xlink:href");
    if (xlink_it != node.attributes.end()) {
        urls.push_back(xlink_it->second);
    }

    for (const auto& child : node.children) {
        CollectURLsRecursive(child, urls);
    }
}

bool IsRemoteURL(const std::string& value) {
    return value.rfind("http://", 0) == 0 || value.rfind("https://", 0) == 0;
}

} // namespace

std::vector<std::string> ResourceResolver::CollectExternalURLs(const XmlNode& node) const {
    std::vector<std::string> urls;
    CollectURLsRecursive(node, urls);
    return urls;
}

bool ResourceResolver::ValidatePolicy(const std::vector<std::string>& urls, const RenderOptions& options, RenderError& error) const {
    if (options.enable_external_resources) {
        return true;
    }

    for (const auto& url : urls) {
        if (IsRemoteURL(url)) {
            error.code = RenderErrorCode::kExternalResourceBlocked;
            error.message = "External resource blocked: " + url;
            return false;
        }
    }
    return true;
}

} // namespace csvg
