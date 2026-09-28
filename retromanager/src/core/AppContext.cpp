#include "retromanager/core/AppContext.hpp"

#include <cassert>

namespace rm {

AppContext::AppContext(std::shared_ptr<IFileSystem> fileSystem, SdLayout layout, std::string platformName,
                       std::string sdRootLabel)
    : fileSystem_(std::move(fileSystem)),
      layout_(std::move(layout)),
      platformName_(std::move(platformName)),
      sdRootLabel_(std::move(sdRootLabel)) {
    assert(fileSystem_ != nullptr);
}

Status AppContext::initialize() {
    for (const std::string& dir : layout_.appDirectories()) {
        Status created = fileSystem_->createDirectories(dir);
        if (!created) return created;
    }
    return success();
}

bool AppContext::isRetroArchInstalled() { return fileSystem_->isDirectory(layout_.retroarchDir); }

}  // namespace rm
