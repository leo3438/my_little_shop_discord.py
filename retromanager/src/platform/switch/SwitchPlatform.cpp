#include "retromanager/platform/LocalFileSystem.hpp"
#include "retromanager/platform/Platform.hpp"

namespace rm {

// libnx mounts the SD card as "sdmc:" before main(); newlib's stdio and
// std::filesystem both understand that prefix.
PlatformServices createPlatformServices() {
    return PlatformServices{
        "Nintendo Switch",
        "sdmc:/",
        std::make_shared<LocalFileSystem>("sdmc:/"),
    };
}

}  // namespace rm
