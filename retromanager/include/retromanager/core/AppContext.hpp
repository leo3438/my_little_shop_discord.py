#pragma once

#include <memory>
#include <string>

#include "retromanager/core/Result.hpp"
#include "retromanager/platform/IFileSystem.hpp"
#include "retromanager/platform/SdLayout.hpp"

namespace rm {

// Composition root: owns the platform services and hands them to modules.
//
// Modules receive what they need from here through their constructor
// (explicit dependency injection). They never reach for globals, which is
// what lets tests build an AppContext around a MemoryFileSystem.
class AppContext {
  public:
    AppContext(std::shared_ptr<IFileSystem> fileSystem, SdLayout layout, std::string platformName = "Unknown",
               std::string sdRootLabel = "/");

    // Prepares RetroManager's own directories. Idempotent.
    Status initialize();

    IFileSystem& fileSystem() { return *fileSystem_; }
    const SdLayout& layout() const { return layout_; }
    const std::string& platformName() const { return platformName_; }
    const std::string& sdRootLabel() const { return sdRootLabel_; }

    // True when RetroArch's base directory is present on the SD card.
    bool isRetroArchInstalled();

  private:
    std::shared_ptr<IFileSystem> fileSystem_;
    SdLayout layout_;
    std::string platformName_;
    std::string sdRootLabel_;
};

}  // namespace rm
