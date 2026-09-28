#include <cstdlib>
#include <filesystem>
#include <system_error>

#include "retromanager/platform/LocalFileSystem.hpp"
#include "retromanager/platform/Platform.hpp"

namespace rm {

#ifndef RM_DESKTOP_DEFAULT_SD_ROOT
#define RM_DESKTOP_DEFAULT_SD_ROOT "sdmc"
#endif

// The desktop build runs against a folder that mimics the SD card.
// RETROMANAGER_SD_ROOT selects it; the default is retromanager/sdmc
// (populate it with tools/make_mock_sd.py).
PlatformServices createPlatformServices() {
    const char* env = std::getenv("RETROMANAGER_SD_ROOT");
    std::filesystem::path root =
        (env != nullptr && *env != '\0') ? std::filesystem::path(env) : std::filesystem::path(RM_DESKTOP_DEFAULT_SD_ROOT);

    std::error_code ec;
    std::filesystem::create_directories(root, ec);
    std::filesystem::path absolute = std::filesystem::absolute(root, ec);
    if (ec) absolute = root;

    return PlatformServices{
        "Desktop",
        absolute.string(),
        std::make_shared<LocalFileSystem>(absolute),
    };
}

}  // namespace rm
