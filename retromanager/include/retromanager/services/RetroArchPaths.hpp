#pragma once

#include <string>
#include <string_view>

#include "retromanager/platform/IFileSystem.hpp"
#include "retromanager/platform/SdLayout.hpp"

namespace rm::retroarch {

// RetroArch is installed when its folder exists on the card. RetroManager
// never creates RetroArch's folders itself.
bool isInstalled(IFileSystem& fs, const SdLayout& layout);

// A directory setting of retroarch.cfg (playlist_directory,
// thumbnails_directory, system_directory...) when it is an absolute SD path;
// `fallback` otherwise (missing file or key, "default", ":/relative" paths).
std::string configuredDirectory(IFileSystem& fs, const SdLayout& layout, std::string_view key,
                                const std::string& fallback);

// The file name RetroArch derives from a playlist label to find its
// thumbnails: &*/:`<>?\| become '_'.
std::string thumbnailName(std::string_view label);

}  // namespace rm::retroarch
