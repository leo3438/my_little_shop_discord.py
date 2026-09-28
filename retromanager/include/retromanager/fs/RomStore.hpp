#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include "retromanager/core/Crc32.hpp"
#include "retromanager/core/Result.hpp"
#include "retromanager/models/GameEntry.hpp"
#include "retromanager/platform/BufferedWriteStream.hpp"
#include "retromanager/platform/IFileSystem.hpp"
#include "retromanager/platform/SdLayout.hpp"

namespace rm {

struct SpaceReport {
    std::uint64_t requiredBytes = 0;             // announced size + safety margin (0 = size unknown)
    std::optional<std::uint64_t> availableBytes;  // nullopt when the platform cannot tell
    bool sufficient = true;
};

// A ROM being written to the SD card. Bytes go through a 1 MiB buffer into
// a hidden staging file (".Game.nds.tmp"), which replaces the final file
// only on a successful commit(). Destroying an uncommitted install deletes
// the staging file: cancellation and errors never leave partial ROMs.
class RomInstall {
  public:
    RomInstall(std::string destination, std::unique_ptr<BufferedWriteStream> stream, std::string expectedCrc32);

    Status write(const char* data, std::size_t size);

    // Verifies the CRC-32 announced by the index (if any), then renames the
    // staging file over the destination.
    Status commit();

    const std::string& destination() const { return destination_; }
    std::uint64_t bytesWritten() const { return written_; }
    std::string crc32() const { return crc_.hex(); }  // of the bytes written so far

  private:
    std::string destination_;
    std::unique_ptr<BufferedWriteStream> stream_;
    std::string expectedCrc32_;
    Crc32 crc_;
    std::uint64_t written_ = 0;
    bool finished_ = false;
};

// Where ROMs live on the SD card: /roms/<system>/<file name>.
// Knows nothing about the network: DownloadService feeds a RomInstall.
class RomStore {
  public:
    // Headroom kept free on top of the ROM size (FAT allocation slack,
    // RetroArch saves written right after launching the game...).
    static constexpr std::uint64_t kSpaceMargin = 1024 * 1024;

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
    Result<std::unique_ptr<RomInstall>> beginInstall(const GameEntry& game);

  private:
    IFileSystem& fs_;
    SdLayout layout_;
};

}  // namespace rm
