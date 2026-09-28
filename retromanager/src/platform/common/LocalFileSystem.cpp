#include "retromanager/platform/LocalFileSystem.hpp"

#include <algorithm>
#include <cstdio>
#include <string>
#include <sys/stat.h>
#include <system_error>

#include "retromanager/platform/VirtualPath.hpp"

namespace fs = std::filesystem;

namespace rm {

namespace {

constexpr std::size_t kIoBufferSize = 64 * 1024;

Error fromErrorCode(const std::error_code& ec, const std::string& context) {
    ErrorCode code = ErrorCode::IoError;
    if (ec == std::errc::no_such_file_or_directory) code = ErrorCode::NotFound;
    else if (ec == std::errc::not_a_directory) code = ErrorCode::NotADirectory;
    else if (ec == std::errc::is_a_directory) code = ErrorCode::IsADirectory;
    else if (ec == std::errc::directory_not_empty) code = ErrorCode::NotEmpty;
    else if (ec == std::errc::file_exists) code = ErrorCode::AlreadyExists;
    else if (ec == std::errc::permission_denied || ec == std::errc::operation_not_permitted ||
             ec == std::errc::read_only_file_system)
        code = ErrorCode::PermissionDenied;
    return makeError(code, context + ": " + ec.message());
}

// POSIX stat(): portable to newlib (Switch) and gives Unix seconds directly,
// unlike std::filesystem::last_write_time whose clock epoch is unspecified in C++17.
std::int64_t hostModificationTime(const fs::path& path) {
    struct stat info {};
    if (::stat(path.string().c_str(), &info) != 0) return 0;
    return static_cast<std::int64_t>(info.st_mtime);
}

// Replaces `target` with `source`. FAT-formatted SD cards refuse to rename
// over an existing file, so fall back to delete-then-rename there.
std::error_code replaceFile(const fs::path& source, const fs::path& target) {
    std::error_code ec;
    fs::rename(source, target, ec);
    if (ec && fs::is_regular_file(target)) {
        std::error_code removeEc;
        fs::remove(target, removeEc);
        ec.clear();
        fs::rename(source, target, ec);
    }
    return ec;
}

class LocalReadStream : public IReadStream {
  public:
    explicit LocalReadStream(std::FILE* file) : file_(file) { std::setvbuf(file_, nullptr, _IOFBF, kIoBufferSize); }
    ~LocalReadStream() override { std::fclose(file_); }

    Result<std::size_t> read(char* buffer, std::size_t size) override {
        std::size_t count = std::fread(buffer, 1, size, file_);
        if (count < size && std::ferror(file_)) return makeError(ErrorCode::IoError, "read failed");
        return count;
    }

  private:
    std::FILE* file_;
};

class LocalWriteStream : public IWriteStream {
  public:
    LocalWriteStream(std::FILE* file, fs::path staging, fs::path target)
        : file_(file), staging_(std::move(staging)), target_(std::move(target)) {
        std::setvbuf(file_, nullptr, _IOFBF, kIoBufferSize);
    }

    ~LocalWriteStream() override {
        if (file_ != nullptr) {  // abandoned: discard the staged data
            std::fclose(file_);
            std::error_code ignored;
            fs::remove(staging_, ignored);
        }
    }

    Status write(const char* data, std::size_t size) override {
        if (file_ == nullptr) return makeError(ErrorCode::IoError, "stream already closed");
        if (std::fwrite(data, 1, size, file_) != size) {
            return makeError(ErrorCode::IoError, "write failed: " + target_.string());
        }
        return success();
    }

    Status close() override {
        if (file_ == nullptr) return makeError(ErrorCode::IoError, "stream already closed");
        bool flushed = std::fflush(file_) == 0;
        bool closed = std::fclose(file_) == 0;
        file_ = nullptr;

        std::error_code ignored;
        if (!flushed || !closed) {
            fs::remove(staging_, ignored);
            return makeError(ErrorCode::IoError, "flush failed: " + target_.string());
        }
        if (std::error_code ec = replaceFile(staging_, target_)) {
            fs::remove(staging_, ignored);
            return fromErrorCode(ec, target_.string());
        }
        return success();
    }

  private:
    std::FILE* file_;
    fs::path staging_;
    fs::path target_;
};

}  // namespace

LocalFileSystem::LocalFileSystem(fs::path root) : root_(std::move(root)) {}

fs::path LocalFileSystem::toHost(const std::string& normalizedPath) const {
    if (normalizedPath == "/") return root_;
    return root_ / normalizedPath.substr(1);
}

Status LocalFileSystem::checkParentDirectory(const std::string& normalizedPath) const {
    std::error_code ec;
    fs::file_status parent = fs::status(toHost(vpath::parent(normalizedPath)), ec);
    if (ec) return fromErrorCode(ec, vpath::parent(normalizedPath));
    if (!fs::is_directory(parent)) return makeError(ErrorCode::NotADirectory, vpath::parent(normalizedPath));
    return success();
}

Result<FileInfo> LocalFileSystem::stat(std::string_view rawPath) {
    auto path = vpath::normalize(rawPath);
    if (!path) return path.error();

    std::error_code ec;
    fs::path host = toHost(path.value());
    fs::file_status status = fs::status(host, ec);
    if (ec) {
        // "/file/child": the path simply does not exist.
        if (ec == std::errc::not_a_directory) return makeError(ErrorCode::NotFound, path.value());
        return fromErrorCode(ec, path.value());
    }
    if (fs::is_directory(status)) return FileInfo{EntryType::Directory, 0, hostModificationTime(host)};
    if (fs::is_regular_file(status)) {
        std::uintmax_t size = fs::file_size(host, ec);
        if (ec) return fromErrorCode(ec, path.value());
        return FileInfo{EntryType::File, static_cast<std::uint64_t>(size), hostModificationTime(host)};
    }
    return makeError(ErrorCode::Unsupported, "not a regular file or directory: " + path.value());
}

Result<std::vector<DirEntry>> LocalFileSystem::listDirectory(std::string_view rawPath) {
    auto path = vpath::normalize(rawPath);
    if (!path) return path.error();

    auto info = stat(path.value());
    if (!info) return info.error();
    if (info.value().type != EntryType::Directory) return makeError(ErrorCode::NotADirectory, path.value());

    std::error_code ec;
    std::vector<DirEntry> entries;
    for (fs::directory_iterator it(toHost(path.value()), ec), end; !ec && it != end; it.increment(ec)) {
        std::string name = it->path().filename().string();
        if (isStagingName(name)) continue;

        std::error_code entryEc;
        fs::file_status status = it->status(entryEc);
        if (entryEc) continue;  // vanished or unreadable entry
        if (fs::is_directory(status)) {
            entries.push_back(DirEntry{std::move(name), EntryType::Directory, 0, hostModificationTime(it->path())});
        } else if (fs::is_regular_file(status)) {
            std::uintmax_t size = it->file_size(entryEc);
            entries.push_back(
                DirEntry{std::move(name), EntryType::File, entryEc ? 0 : size, hostModificationTime(it->path())});
        }
    }
    if (ec) return fromErrorCode(ec, path.value());

    std::sort(entries.begin(), entries.end(),
              [](const DirEntry& a, const DirEntry& b) { return a.name < b.name; });
    return entries;
}

Status LocalFileSystem::createDirectories(std::string_view rawPath) {
    auto path = vpath::normalize(rawPath);
    if (!path) return path.error();
    if (path.value() == "/") return success();

    // Walk component by component so that "a component is a file" is
    // reported identically on every host filesystem.
    std::string current;
    std::size_t pos = 1;
    while (pos <= path.value().size()) {
        std::size_t end = path.value().find('/', pos);
        if (end == std::string::npos) end = path.value().size();
        current = path.value().substr(0, end);
        pos = end + 1;

        std::error_code ec;
        fs::path host = toHost(current);
        fs::file_status status = fs::status(host, ec);
        if (!ec) {
            if (!fs::is_directory(status)) return makeError(ErrorCode::NotADirectory, current);
            continue;
        }
        if (ec != std::errc::no_such_file_or_directory) return fromErrorCode(ec, current);
        fs::create_directory(host, ec);
        if (ec) return fromErrorCode(ec, current);
    }
    return success();
}

Result<std::unique_ptr<IReadStream>> LocalFileSystem::openRead(std::string_view rawPath) {
    auto path = vpath::normalize(rawPath);
    if (!path) return path.error();

    auto info = stat(path.value());
    if (!info) return info.error();
    if (info.value().type == EntryType::Directory) return makeError(ErrorCode::IsADirectory, path.value());

    std::FILE* file = std::fopen(toHost(path.value()).string().c_str(), "rb");
    if (file == nullptr) return makeError(ErrorCode::IoError, "cannot open " + path.value());
    return std::unique_ptr<IReadStream>(std::make_unique<LocalReadStream>(file));
}

Result<std::unique_ptr<IWriteStream>> LocalFileSystem::openWrite(std::string_view rawPath) {
    auto path = vpath::normalize(rawPath);
    if (!path) return path.error();
    if (path.value() == "/") return makeError(ErrorCode::IsADirectory, "/");

    if (Status parent = checkParentDirectory(path.value()); !parent) return parent.error();
    if (isDirectory(path.value())) return makeError(ErrorCode::IsADirectory, path.value());

    fs::path target = toHost(path.value());
    fs::path staging = toHost(stagingPath(path.value()));

    std::FILE* file = std::fopen(staging.string().c_str(), "wb");
    if (file == nullptr) return makeError(ErrorCode::IoError, "cannot create " + path.value());
    return std::unique_ptr<IWriteStream>(std::make_unique<LocalWriteStream>(file, staging, target));
}

Status LocalFileSystem::remove(std::string_view rawPath) {
    auto path = vpath::normalize(rawPath);
    if (!path) return path.error();
    if (path.value() == "/") return makeError(ErrorCode::InvalidPath, "cannot remove the root");

    auto info = stat(path.value());
    if (!info) return info.error();

    std::error_code ec;
    fs::path host = toHost(path.value());
    if (info.value().type == EntryType::Directory && !fs::is_empty(host, ec)) {
        return makeError(ErrorCode::NotEmpty, path.value());
    }
    fs::remove(host, ec);
    if (ec) return fromErrorCode(ec, path.value());
    return success();
}

Status LocalFileSystem::removeAll(std::string_view rawPath) {
    auto path = vpath::normalize(rawPath);
    if (!path) return path.error();
    if (path.value() == "/") return makeError(ErrorCode::InvalidPath, "cannot remove the root");

    std::error_code ec;
    fs::remove_all(toHost(path.value()), ec);
    if (ec && ec != std::errc::no_such_file_or_directory) return fromErrorCode(ec, path.value());
    return success();
}

Status LocalFileSystem::rename(std::string_view rawFrom, std::string_view rawTo) {
    auto from = vpath::normalize(rawFrom);
    if (!from) return from.error();
    auto to = vpath::normalize(rawTo);
    if (!to) return to.error();
    if (from.value() == "/" || to.value() == "/") return makeError(ErrorCode::InvalidPath, "cannot move the root");

    auto source = stat(from.value());
    if (!source) return source.error();
    if (from.value() == to.value()) return success();
    if (vpath::isWithin(to.value(), from.value())) {
        return makeError(ErrorCode::InvalidPath, "cannot move a directory into itself");
    }
    if (Status parent = checkParentDirectory(to.value()); !parent) return parent;

    auto target = stat(to.value());
    if (target.ok()) {
        if (target.value().type == EntryType::Directory) return makeError(ErrorCode::AlreadyExists, to.value());
        if (source.value().type == EntryType::Directory) return makeError(ErrorCode::NotADirectory, to.value());
    }

    if (std::error_code ec = replaceFile(toHost(from.value()), toHost(to.value()))) {
        return fromErrorCode(ec, from.value() + " -> " + to.value());
    }
    return success();
}

Result<std::uint64_t> LocalFileSystem::availableSpace(std::string_view rawPath) {
    auto path = vpath::normalize(rawPath);
    if (!path) return path.error();
    if (auto info = stat(path.value()); !info) return info.error();

    std::error_code ec;
    fs::space_info space = fs::space(toHost(path.value()), ec);
    if (ec) return makeError(ErrorCode::Unsupported, "cannot query free space: " + ec.message());
    return static_cast<std::uint64_t>(space.available);
}

}  // namespace rm
