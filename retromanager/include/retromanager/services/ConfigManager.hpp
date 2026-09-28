#pragma once

#include <string>

#include "retromanager/core/Result.hpp"
#include "retromanager/models/AppConfig.hpp"
#include "retromanager/platform/IFileSystem.hpp"

namespace rm {

// Reads and writes config.json on the SD card, through IFileSystem only.
class ConfigManager {
  public:
    // `path` is a virtual path, normally SdLayout::appConfig.
    ConfigManager(IFileSystem& fs, std::string path);

    // Missing file: writes the defaults (so the user has a template to
    // edit) and returns them. Unreadable or invalid file: returns the error
    // and leaves the file untouched, so a typo never destroys settings.
    Result<AppConfig> loadOrCreate();

    // Atomic replace; creates the parent directory if needed.
    Status save(const AppConfig& config);

    const std::string& path() const { return path_; }

  private:
    IFileSystem& fs_;
    std::string path_;
};

}  // namespace rm
