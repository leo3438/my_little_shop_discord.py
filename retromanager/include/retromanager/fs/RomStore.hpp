#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include "retromanager/core/Result.hpp"
#include "retromanager/models/GameEntry.hpp"
#include "retromanager/fs/FileInstall.hpp"
#include "retromanager/platform/IFileSystem.hpp"
#include "retromanager/platform/SdLayout.hpp"

namespace rm {

// Where ROMs live on the SD card: /roms/<system>/<file name>.
// Knows nothing about the network: DownloadService feeds a FileInstall.
class RomStore {
  public:
    static constexpr std::uint64_t kSpaceMargin = rm::kSpaceMargin;

    RomStore(IFileSystem& fs, SdLayout layout);

    // Destination virtual path. File names are sanitized for FAT32/exFAT
    // (<>:"|?*\ and control characters become '_', trailing dots and spaces
    // are dropped); InvalidArgument when nothing usable is left.
    Result<std::string> destinationFor(const GameEntry& game) const;

    bool isInstalled(const GameEntry& game);

    SpaceReport spaceReport(const GameEntry& game);

    // Creates the system directory, checks free space (InsufficientSpace
    // with the numbers in the message) and opens the staged, buffered
    // output. Nothing is created on the card when the check fails.
    Result<std::unique_ptr<FileInstall>> beginInstall(const GameEntry& game);

  private:
    IFileSystem& fs_;
    SdLayout layout_;
};

}  // namespace rm
