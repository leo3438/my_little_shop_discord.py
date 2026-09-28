#pragma once

#include <memory>
#include <string>

#include "retromanager/platform/IFileSystem.hpp"

namespace rm {

// Concrete services for the platform the binary was built for. Exactly one
// implementation of createPlatformServices() is linked in:
// src/platform/switch/ or src/platform/desktop/.
struct PlatformServices {
    std::string name;         // "Nintendo Switch", "Desktop"
    std::string sdRootLabel;  // human readable root, e.g. "sdmc:/" or a host folder
    std::shared_ptr<IFileSystem> fileSystem;
};

PlatformServices createPlatformServices();

}  // namespace rm
