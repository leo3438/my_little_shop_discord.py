#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "retromanager/core/Result.hpp"

namespace rm {

enum class EntryType { File, Directory };

struct FileInfo {
    EntryType type;
    std::uint64_t size;  // 0 for directories
};

struct DirEntry {
    std::string name;  // bare name, no path
    EntryType type;
    std::uint64_t size;

    bool operator==(const DirEntry& other) const {
        return name == other.name && type == other.type && size == other.size;
    }
};

class IReadStream {
  public:
    virtual ~IReadStream() = default;
    // Reads up to `size` bytes. Returns 0 at end of file.
    virtual Result<std::size_t> read(char* buffer, std::size_t size) = 0;
};

// Writes are staged and only become visible when close() succeeds, so a
// crash or a dropped connection never leaves a truncated save or ROM behind.
// Destroying a stream without calling close() discards the staged data.
class IWriteStream {
  public:
    virtual ~IWriteStream() = default;
    virtual Status write(const char* data, std::size_t size) = 0;
    virtual Status close() = 0;
};

// The single entry point to the SD card. Every module goes through this
// interface; none of them may touch <filesystem>, fopen or libnx fs directly.
//
// All paths are virtual paths (see VirtualPath.hpp). Implementations must
// normalize them and reject invalid ones with ErrorCode::InvalidPath.
//
// Implementations: LocalFileSystem (console "sdmc:/" and desktop mock
// folder) and MemoryFileSystem (unit tests). The contract test suite in
// tests/unit/FileSystemContractTest.cpp is run against each of them.
class IFileSystem {
  public:
    virtual ~IFileSystem() = default;

    virtual Result<FileInfo> stat(std::string_view path) = 0;

    // Entries sorted by name. NotFound / NotADirectory on bad input.
    virtual Result<std::vector<DirEntry>> listDirectory(std::string_view path) = 0;

    // Creates the directory and its missing parents. Succeeds if it exists.
    virtual Status createDirectories(std::string_view path) = 0;

    virtual Result<std::unique_ptr<IReadStream>> openRead(std::string_view path) = 0;

    // The parent directory must exist. Replaces an existing file atomically
    // on close().
    virtual Result<std::unique_ptr<IWriteStream>> openWrite(std::string_view path) = 0;

    // Removes a file or an empty directory.
    virtual Status remove(std::string_view path) = 0;

    // Removes a file or a directory tree. Succeeds if the path is missing.
    virtual Status removeAll(std::string_view path) = 0;

    // Moves a file or directory. An existing destination file is replaced.
    virtual Status rename(std::string_view from, std::string_view to) = 0;

    // Convenience helpers built on the primitives above (shared by every
    // implementation, so they behave identically everywhere).
    bool exists(std::string_view path);
    bool isFile(std::string_view path);
    bool isDirectory(std::string_view path);
    Result<std::string> readFile(std::string_view path);
    Status writeFile(std::string_view path, std::string_view data);
};

}  // namespace rm
