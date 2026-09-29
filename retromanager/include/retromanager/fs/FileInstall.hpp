#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "retromanager/core/Crc32.hpp"
#include "retromanager/core/Result.hpp"
#include "retromanager/platform/BufferedWriteStream.hpp"
#include "retromanager/platform/IFileSystem.hpp"

namespace rm {

// Headroom kept free on top of a download (FAT allocation slack, RetroArch
// saves written right after launching the game...).
constexpr std::uint64_t kSpaceMargin = 1024 * 1024;

struct SpaceReport {
    std::uint64_t requiredBytes = 0;             // announced size + safety margin (0 = size unknown)
    std::optional<std::uint64_t> availableBytes;  // nullopt when the platform cannot tell
    bool sufficient = true;
};

// Room for `sizeBytes` at `destination` (the closest existing ancestor is
// queried: the folder may not exist yet). Unknown size or unknown free
// space never blocks.
SpaceReport checkSpace(IFileSystem& fs, const std::string& destination, std::uint64_t sizeBytes);

// A file being written to the SD card (ROM, homebrew, BIOS...). Bytes go
// through a 1 MiB buffer into a hidden staging file (".Game.nds.tmp"),
// which replaces the final file only on a successful commit(). Destroying
// an uncommitted install deletes the staging file: cancellation and errors
// never leave partial files.
class FileInstall {
  public:
    FileInstall(std::string destination, std::unique_ptr<BufferedWriteStream> stream, std::string expectedCrc32);

    // `check` sees the first `bytes` bytes as soon as they are received; its
    // error fails that write (the transfer stops early: an HTML error page
    // is not downloaded to the end) and a shorter file fails at commit().
    void setHeaderCheck(std::size_t bytes, std::function<Status(std::string_view)> check);

    Status write(const char* data, std::size_t size);

    // Verifies the header and the expected CRC-32 (if any), then renames the
    // staging file over the destination.
    Status commit();

    const std::string& destination() const { return destination_; }
    std::uint64_t bytesWritten() const { return written_; }
    std::string crc32() const { return crc_.hex(); }  // of the bytes written so far

  private:
    Status checkHeaderIfComplete();

    std::string destination_;
    std::unique_ptr<BufferedWriteStream> stream_;
    std::string expectedCrc32_;
    Crc32 crc_;
    std::uint64_t written_ = 0;
    bool finished_ = false;

    std::size_t headerBytes_ = 0;
    std::function<Status(std::string_view)> headerCheck_;
    std::string header_;
    bool headerChecked_ = true;
};

// Space check (InsufficientSpace with the numbers, `what` naming the item
// in the message), parent folder creation, staged buffered output. Nothing
// is created on the card when the check fails.
Result<std::unique_ptr<FileInstall>> beginFileInstall(IFileSystem& fs, const std::string& destination,
                                                      std::uint64_t sizeBytes, std::string expectedCrc32,
                                                      const std::string& what);

}  // namespace rm
