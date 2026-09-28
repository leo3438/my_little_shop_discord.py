#include "retromanager/services/RetroArchPaths.hpp"

#include "retromanager/parsers/CfgDocument.hpp"
#include "retromanager/platform/VirtualPath.hpp"

namespace rm::retroarch {

bool isInstalled(IFileSystem& fs, const SdLayout& layout) { return fs.isDirectory(layout.retroarchDir); }

std::string configuredDirectory(IFileSystem& fs, const SdLayout& layout, std::string_view key,
                                const std::string& fallback) {
    auto cfg = fs.readFile(layout.retroarchCfg);
    if (!cfg) return fallback;
    auto configured = CfgDocument::parse(cfg.value()).get(key);
    // Only absolute SD paths are understood; ":/cheats" (relative to the
    // RetroArch binary) and "default" keep the standard location.
    if (!configured || configured->empty() || configured->front() != '/') return fallback;
    auto normalized = vpath::normalize(*configured);
    return normalized ? normalized.value() : fallback;
}

std::string thumbnailName(std::string_view label) {
    std::string name(label);
    for (char& c : name) {
        switch (c) {
            case '&': case '*': case '/': case ':': case '`': case '<': case '>': case '?': case '\\': case '|':
                c = '_';
                break;
            default: break;
        }
    }
    return name;
}

}  // namespace rm::retroarch
