#pragma once

#include <filesystem>

#include "retromanager/platform/IFileSystem.hpp"

namespace rm {

// IFileSystem backed by a real directory, used as a sandbox root.
//
// - On the console the root is "sdmc:/" (the SD card mounted by libnx).
// - On desktop the root is a local folder that mimics the SD card layout
//   (see tools/make_mock_sd.py), so the whole app runs against fake data.
//
// Virtual paths are normalized before being mapped under the root, so no
// path can ever resolve outside of it.
class LocalFileSystem : public IFileSystem {
  public:
    explicit LocalFileSystem(std::filesystem::path root);

    const std::filesystem::path& root() const { return root_; }

    Result<FileInfo> stat(std::string_view path) override;
    Result<std::vector<DirEntry>> listDirectory(std::string_view path) override;
    Status createDirectories(std::string_view path) override;
    Result<std::unique_ptr<IReadStream>> openRead(std::string_view path) override;
    Result<std::unique_ptr<IWriteStream>> openWrite(std::string_view path, WriteOptions options = {}) override;
    Status remove(std::string_view path) override;
    Status removeAll(std::string_view path) override;
    Status rename(std::string_view from, std::string_view to) override;
    Result<std::uint64_t> availableSpace(std::string_view path) override;

  private:
    std::filesystem::path toHost(const std::string& normalizedPath) const;
    Status checkParentDirectory(const std::string& normalizedPath) const;

    std::filesystem::path root_;
};

}  // namespace rm
