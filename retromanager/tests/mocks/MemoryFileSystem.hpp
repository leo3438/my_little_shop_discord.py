#pragma once

#include <ctime>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "retromanager/platform/IFileSystem.hpp"

namespace rm::test {

// Fully in-memory IFileSystem: fast, hermetic, no host disk access.
// Passes the same contract suite as LocalFileSystem.
class MemoryFileSystem : public IFileSystem {
  public:
    MemoryFileSystem();

    Result<FileInfo> stat(std::string_view path) override;
    Result<std::vector<DirEntry>> listDirectory(std::string_view path) override;
    Status createDirectories(std::string_view path) override;
    Result<std::unique_ptr<IReadStream>> openRead(std::string_view path) override;
    Result<std::unique_ptr<IWriteStream>> openWrite(std::string_view path, WriteOptions options = {}) override;
    Status remove(std::string_view path) override;
    Status removeAll(std::string_view path) override;
    Status rename(std::string_view from, std::string_view to) override;
    // capacity minus the bytes of all stored files.
    Result<std::uint64_t> availableSpace(std::string_view path) override;

    // Test hook: every mutation fails with PermissionDenied (simulates a
    // write-protected or unplugged SD card).
    void setReadOnly(bool readOnly) { readOnly_ = readOnly; }

    // Test hook: simulated card size (default 64 GiB). nullopt makes
    // availableSpace() report Unsupported, like a platform that cannot tell.
    void setCapacity(std::optional<std::uint64_t> bytes) { capacity_ = bytes; }
    std::uint64_t usedBytes() const;

    // Test hook: the clock stamping writes (default: the real time), and a
    // way to backdate a file.
    void setClock(std::function<std::int64_t()> clock) { clock_ = std::move(clock); }
    Status setModificationTime(std::string_view path, std::int64_t modifiedAt);

    // Number of files and directories, root excluded.
    std::size_t nodeCount() const { return nodes_.size() - 1; }

  private:
    struct Node {
        EntryType type;
        std::shared_ptr<const std::string> data;  // files only
        std::int64_t modifiedAt = 0;
    };

    friend class MemoryWriteStream;

    Status commit(const std::string& path, std::string data);
    Status keepStaging(const std::string& path, std::string data);
    void dropStaging(const std::string& path);
    Status checkWritable() const;
    Status checkParentDirectory(const std::string& path) const;
    using NodeMap = std::map<std::string, Node>;
    using Range = std::pair<NodeMap::iterator, NodeMap::iterator>;

    Range descendants(const std::string& path);
    bool hasChildren(const std::string& path);

    NodeMap nodes_;  // key: normalized virtual path
    bool readOnly_ = false;
    std::function<std::int64_t()> clock_ = [] { return static_cast<std::int64_t>(std::time(nullptr)); };
    std::optional<std::uint64_t> capacity_ = 64ull * 1024 * 1024 * 1024;
};

}  // namespace rm::test
