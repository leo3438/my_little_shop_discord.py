#include "MemoryFileSystem.hpp"

#include <algorithm>
#include <cstring>
#include <vector>

#include "retromanager/platform/VirtualPath.hpp"

namespace rm::test {

namespace {

std::string childPrefix(const std::string& path) { return path == "/" ? "/" : path + "/"; }

bool startsWith(const std::string& value, const std::string& prefix) {
    return value.size() >= prefix.size() && value.compare(0, prefix.size(), prefix) == 0;
}

class MemoryReadStream : public IReadStream {
  public:
    explicit MemoryReadStream(std::shared_ptr<const std::string> data) : data_(std::move(data)) {}

    Result<std::size_t> read(char* buffer, std::size_t size) override {
        std::size_t count = std::min(size, data_->size() - offset_);
        std::memcpy(buffer, data_->data() + offset_, count);
        offset_ += count;
        return count;
    }

  private:
    std::shared_ptr<const std::string> data_;  // snapshot: later writes don't affect an open reader
    std::size_t offset_ = 0;
};

}  // namespace

// The filesystem must outlive its write streams.
// Bytes stay in the stream until close() (published) or suspend() (kept as
// the staging node, like the host's hidden .tmp file).
class MemoryWriteStream : public IWriteStream {
  public:
    MemoryWriteStream(MemoryFileSystem& fs, std::string path, std::string resumed)
        : fs_(fs), path_(std::move(path)), buffer_(std::move(resumed)), resumedFrom_(buffer_.size()) {}

    ~MemoryWriteStream() override {
        if (!closed_) fs_.dropStaging(path_);  // abandoned: discard, like a deleted .tmp
    }

    Status write(const char* data, std::size_t size) override {
        if (closed_) return makeError(ErrorCode::IoError, "stream already closed");
        buffer_.append(data, size);
        return success();
    }

    Status close() override {
        if (closed_) return makeError(ErrorCode::IoError, "stream already closed");
        closed_ = true;
        fs_.dropStaging(path_);
        return fs_.commitFile(path_, std::move(buffer_));
    }

    Status suspend() override {
        if (closed_) return makeError(ErrorCode::IoError, "stream already closed");
        closed_ = true;
        return fs_.keepStaging(path_, std::move(buffer_));
    }

    std::uint64_t resumedFrom() const override { return resumedFrom_; }

  private:
    MemoryFileSystem& fs_;
    std::string path_;
    std::string buffer_;
    std::uint64_t resumedFrom_ = 0;
    bool closed_ = false;
};

MemoryFileSystem::MemoryFileSystem() { nodes_.emplace("/", Node{EntryType::Directory, nullptr, clock_()}); }

Status MemoryFileSystem::setModificationTime(std::string_view rawPath, std::int64_t modifiedAt) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto path = vpath::normalize(rawPath);
    if (!path) return path.error();
    auto it = nodes_.find(path.value());
    if (it == nodes_.end()) return makeError(ErrorCode::NotFound, path.value());
    it->second.modifiedAt = modifiedAt;
    return success();
}

Status MemoryFileSystem::checkWritable() const {
    if (readOnly_) return makeError(ErrorCode::PermissionDenied, "filesystem is read-only");
    return success();
}

Status MemoryFileSystem::checkParentDirectory(const std::string& path) const {
    auto parent = nodes_.find(vpath::parent(path));
    if (parent == nodes_.end()) {
        // Distinguish "a parent is missing" from "a parent is a file".
        for (std::string ancestor = vpath::parent(path); ancestor != "/"; ancestor = vpath::parent(ancestor)) {
            auto node = nodes_.find(ancestor);
            if (node != nodes_.end() && node->second.type == EntryType::File) {
                return makeError(ErrorCode::NotADirectory, ancestor);
            }
        }
        return makeError(ErrorCode::NotFound, "parent directory missing: " + vpath::parent(path));
    }
    if (parent->second.type != EntryType::Directory) {
        return makeError(ErrorCode::NotADirectory, parent->first);
    }
    return success();
}

MemoryFileSystem::Range MemoryFileSystem::descendants(const std::string& path) {
    // Keys are sorted bytewise, so "/roms-old" sits between "/roms" and
    // "/roms/..."; start from the prefix, not from the node itself.
    std::string prefix = childPrefix(path);
    auto begin = nodes_.lower_bound(prefix);
    if (begin != nodes_.end() && begin->first == path) ++begin;  // root: prefix == path
    auto end = begin;
    while (end != nodes_.end() && startsWith(end->first, prefix)) ++end;
    return {begin, end};
}

bool MemoryFileSystem::hasChildren(const std::string& path) {
    Range range = descendants(path);
    return range.first != range.second;
}

Result<FileInfo> MemoryFileSystem::stat(std::string_view rawPath) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto path = vpath::normalize(rawPath);
    if (!path) return path.error();

    auto it = nodes_.find(path.value());
    if (it == nodes_.end()) return makeError(ErrorCode::NotFound, path.value());
    std::uint64_t size = it->second.type == EntryType::File ? it->second.data->size() : 0;
    return FileInfo{it->second.type, size, it->second.modifiedAt};
}

Result<std::vector<DirEntry>> MemoryFileSystem::listDirectory(std::string_view rawPath) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto path = vpath::normalize(rawPath);
    if (!path) return path.error();

    auto dir = nodes_.find(path.value());
    if (dir == nodes_.end()) return makeError(ErrorCode::NotFound, path.value());
    if (dir->second.type != EntryType::Directory) return makeError(ErrorCode::NotADirectory, path.value());

    std::string prefix = childPrefix(path.value());
    std::vector<DirEntry> entries;
    Range range = descendants(path.value());
    for (auto it = range.first; it != range.second; ++it) {
        std::string name = it->first.substr(prefix.size());
        if (name.find('/') != std::string::npos) continue;  // grand-child
        if (isStagingName(name)) continue;                   // suspended downloads stay hidden
        std::uint64_t size = it->second.type == EntryType::File ? it->second.data->size() : 0;
        entries.push_back(DirEntry{std::move(name), it->second.type, size, it->second.modifiedAt});
    }
    std::sort(entries.begin(), entries.end(),
              [](const DirEntry& a, const DirEntry& b) { return a.name < b.name; });
    return entries;
}

Status MemoryFileSystem::createDirectories(std::string_view rawPath) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto path = vpath::normalize(rawPath);
    if (!path) return path.error();

    std::vector<std::string> missing;
    for (std::string current = path.value(); current != "/"; current = vpath::parent(current)) {
        auto it = nodes_.find(current);
        if (it != nodes_.end()) {
            if (it->second.type != EntryType::Directory) return makeError(ErrorCode::NotADirectory, current);
            break;
        }
        missing.push_back(current);
    }
    if (missing.empty()) return success();

    if (Status writable = checkWritable(); !writable) return writable;
    for (const std::string& dir : missing) nodes_.emplace(dir, Node{EntryType::Directory, nullptr, clock_()});
    return success();
}

Result<std::unique_ptr<IReadStream>> MemoryFileSystem::openRead(std::string_view rawPath) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto path = vpath::normalize(rawPath);
    if (!path) return path.error();

    auto it = nodes_.find(path.value());
    if (it == nodes_.end()) return makeError(ErrorCode::NotFound, path.value());
    if (it->second.type == EntryType::Directory) return makeError(ErrorCode::IsADirectory, path.value());
    return std::unique_ptr<IReadStream>(std::make_unique<MemoryReadStream>(it->second.data));
}

Result<std::unique_ptr<IWriteStream>> MemoryFileSystem::openWrite(std::string_view rawPath, WriteOptions options) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto path = vpath::normalize(rawPath);
    if (!path) return path.error();
    if (Status writable = checkWritable(); !writable) return writable.error();

    auto it = nodes_.find(path.value());
    if (it != nodes_.end() && it->second.type == EntryType::Directory) {
        return makeError(ErrorCode::IsADirectory, path.value());
    }
    if (Status parent = checkParentDirectory(path.value()); !parent) return parent.error();

    std::string resumed;
    auto staging = nodes_.find(stagingPath(path.value()));
    if (staging != nodes_.end() && staging->second.type == EntryType::File) {
        if (options.resume) resumed = *staging->second.data;
        nodes_.erase(staging);  // owned by the new stream from now on
    }
    return std::unique_ptr<IWriteStream>(std::make_unique<MemoryWriteStream>(*this, path.value(), std::move(resumed)));
}

Status MemoryFileSystem::keepStaging(const std::string& path, std::string data) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (Status writable = checkWritable(); !writable) return writable;
    nodes_[stagingPath(path)] = Node{EntryType::File, std::make_shared<const std::string>(std::move(data)), clock_()};
    return success();
}

void MemoryFileSystem::dropStaging(const std::string& path) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    nodes_.erase(stagingPath(path));
}

Status MemoryFileSystem::commitFile(const std::string& path, std::string data) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (Status writable = checkWritable(); !writable) return writable;
    if (Status parent = checkParentDirectory(path); !parent) return parent;

    auto it = nodes_.find(path);
    if (it != nodes_.end() && it->second.type == EntryType::Directory) {
        return makeError(ErrorCode::IsADirectory, path);
    }
    nodes_[path] = Node{EntryType::File, std::make_shared<const std::string>(std::move(data)), clock_()};
    return success();
}

Status MemoryFileSystem::remove(std::string_view rawPath) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto path = vpath::normalize(rawPath);
    if (!path) return path.error();
    if (path.value() == "/") return makeError(ErrorCode::InvalidPath, "cannot remove the root");
    if (Status writable = checkWritable(); !writable) return writable;

    auto it = nodes_.find(path.value());
    if (it == nodes_.end()) return makeError(ErrorCode::NotFound, path.value());
    if (it->second.type == EntryType::Directory && hasChildren(path.value())) {
        return makeError(ErrorCode::NotEmpty, path.value());
    }
    nodes_.erase(it);
    return success();
}

Status MemoryFileSystem::removeAll(std::string_view rawPath) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto path = vpath::normalize(rawPath);
    if (!path) return path.error();
    if (path.value() == "/") return makeError(ErrorCode::InvalidPath, "cannot remove the root");
    if (Status writable = checkWritable(); !writable) return writable;

    auto it = nodes_.find(path.value());
    if (it == nodes_.end()) return success();

    Range range = descendants(path.value());
    nodes_.erase(range.first, range.second);
    nodes_.erase(path.value());
    return success();
}

Status MemoryFileSystem::rename(std::string_view rawFrom, std::string_view rawTo) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto from = vpath::normalize(rawFrom);
    if (!from) return from.error();
    auto to = vpath::normalize(rawTo);
    if (!to) return to.error();
    if (from.value() == "/" || to.value() == "/") return makeError(ErrorCode::InvalidPath, "cannot move the root");
    if (Status writable = checkWritable(); !writable) return writable;

    auto source = nodes_.find(from.value());
    if (source == nodes_.end()) return makeError(ErrorCode::NotFound, from.value());
    if (from.value() == to.value()) return success();
    if (vpath::isWithin(to.value(), from.value())) {
        return makeError(ErrorCode::InvalidPath, "cannot move a directory into itself");
    }
    if (Status parent = checkParentDirectory(to.value()); !parent) return parent;

    auto target = nodes_.find(to.value());
    if (target != nodes_.end()) {
        if (target->second.type == EntryType::Directory) return makeError(ErrorCode::AlreadyExists, to.value());
        if (source->second.type == EntryType::Directory) return makeError(ErrorCode::NotADirectory, to.value());
        nodes_.erase(target);
        source = nodes_.find(from.value());
    }

    // Collect the node and its subtree, then re-insert under the new name.
    std::vector<std::pair<std::string, Node>> moved;
    moved.emplace_back(to.value(), source->second);
    Range range = descendants(from.value());
    for (auto it = range.first; it != range.second; ++it) {
        moved.emplace_back(to.value() + it->first.substr(from.value().size()), it->second);
    }
    nodes_.erase(range.first, range.second);
    nodes_.erase(from.value());
    for (auto& entry : moved) nodes_.insert(std::move(entry));
    return success();
}

std::uint64_t MemoryFileSystem::usedBytes() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    std::uint64_t used = 0;
    for (const auto& entry : nodes_) {
        if (entry.second.type == EntryType::File) used += entry.second.data->size();
    }
    return used;
}

Result<std::uint64_t> MemoryFileSystem::availableSpace(std::string_view rawPath) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto path = vpath::normalize(rawPath);
    if (!path) return path.error();
    if (nodes_.find(path.value()) == nodes_.end()) return makeError(ErrorCode::NotFound, path.value());
    if (!capacity_) return makeError(ErrorCode::Unsupported, "free space unknown (simulated)");
    std::uint64_t used = usedBytes();
    return used >= *capacity_ ? 0 : *capacity_ - used;
}

}  // namespace rm::test
