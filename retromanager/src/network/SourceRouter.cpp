#include "retromanager/network/SourceRouter.hpp"

#include <algorithm>
#include <cctype>
#include <optional>

#include "retromanager/core/Url.hpp"

namespace rm {

namespace {

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

// "ftp" and "ftps" reach the same NAS (explicit TLS on the same port). An
// SMB server is one source per share (each may have its own account).
struct ServerKey {
    std::string family;
    std::string host;
    std::uint16_t port = 0;
    std::string share;  // smb only, lowercase
    bool operator==(const ServerKey& o) const {
        return family == o.family && host == o.host && port == o.port && share == o.share;
    }
};

std::optional<ServerKey> serverOf(const std::string& address) {
    auto parts = url::split(url::encodeForTransfer(address));
    if (!parts) return std::nullopt;
    const std::string scheme = parts.value().scheme;
    ServerKey key;
    key.host = lower(parts.value().host);
    if (scheme == "ftp" || scheme == "ftps") {
        key.family = "ftp";
        key.port = parts.value().port.value_or(21);
    } else if (scheme == "http" || scheme == "https") {
        key.family = scheme;  // http and https are different servers (and trust levels)
        key.port = parts.value().port.value_or(scheme == "https" ? 443 : 80);
    } else if (scheme == "smb") {
        key.family = "smb";
        key.port = parts.value().port.value_or(445);
        std::string path = url::percentDecode(parts.value().path);
        key.share = lower(path.substr(1, path.find('/', 1) == std::string::npos ? std::string::npos : path.find('/', 1) - 1));
    } else {
        return std::nullopt;
    }
    return key;
}

}  // namespace

SourceRouter::SourceRouter(std::shared_ptr<IRemoteSource> publicWeb) : publicWeb_(std::move(publicWeb)) {}

bool SourceRouter::add(std::string name, std::string type, std::shared_ptr<IRemoteSource> source) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const Entry& entry : entries_) {
        if (lower(entry.name) == lower(name)) return false;
    }
    if (entries_.empty()) active_ = name;
    entries_.push_back(Entry{std::move(name), std::move(type), std::move(source)});
    return true;
}

bool SourceRouter::remove(const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = std::find_if(entries_.begin(), entries_.end(), [&](const Entry& e) { return lower(e.name) == lower(name); });
    if (it == entries_.end()) return false;
    const bool wasActive = it->name == active_;
    entries_.erase(it);  // shared_ptr: a running transfer keeps its copy
    if (wasActive) active_ = entries_.empty() ? "" : entries_.front().name;
    return true;
}

bool SourceRouter::replace(const std::string& name, std::string newName, std::string type,
                           std::shared_ptr<IRemoteSource> source) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = std::find_if(entries_.begin(), entries_.end(), [&](const Entry& e) { return lower(e.name) == lower(name); });
    if (it == entries_.end()) return false;
    for (const Entry& other : entries_) {
        if (&other != &*it && lower(other.name) == lower(newName)) return false;
    }
    const bool wasActive = it->name == active_;
    *it = Entry{std::move(newName), std::move(type), std::move(source)};  // shared_ptr: a running transfer keeps the old client
    if (wasActive) active_ = it->name;
    return true;
}

std::shared_ptr<IRemoteSource> SourceRouter::sourceNamed(const std::string& name) const {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const Entry& entry : entries_) {
        if (lower(entry.name) == lower(name)) return entry.source;
    }
    return nullptr;
}

bool SourceRouter::setActive(const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const Entry& entry : entries_) {
        if (lower(entry.name) == lower(name)) {
            active_ = entry.name;
            return true;
        }
    }
    return false;
}

std::string SourceRouter::activeName() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return active_;
}

std::vector<SourceInfo> SourceRouter::sources() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<SourceInfo> list;
    for (const Entry& entry : entries_) {
        list.push_back(SourceInfo{entry.name, entry.type, entry.source->describe(), entry.name == active_});
    }
    return list;
}

std::shared_ptr<IRemoteSource> SourceRouter::active() const {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const Entry& entry : entries_) {
        if (entry.name == active_) return entry.source;
    }
    return std::make_shared<UnavailableRemoteSource>(
        makeError(ErrorCode::NotConfigured, "no source configured: add one in the Sources screen"), "(no source)");
}

std::shared_ptr<IRemoteSource> SourceRouter::route(const std::string& address) const {
    auto target = serverOf(address);
    std::lock_guard<std::mutex> lock(mutex_);
    if (target) {
        std::shared_ptr<IRemoteSource> match;
        for (const Entry& entry : entries_) {
            auto own = serverOf(entry.source->indexUrl());
            if (own && *own == *target && (!match || entry.name == active_)) match = entry.source;  // active wins a tie
        }
        if (match) return match;
        if (publicWeb_ && (target->family == "http" || target->family == "https")) return publicWeb_;
    }
    return std::make_shared<UnavailableRemoteSource>(
        makeError(ErrorCode::PermissionDenied, "no configured source for " + address), "(unknown server)");
}

std::string SourceRouter::describe() const { return active()->describe(); }
std::string SourceRouter::indexUrl() const { return active()->indexUrl(); }
Result<std::string> SourceRouter::fetchIndex() { return active()->fetchIndex(); }

Status SourceRouter::downloadFile(const std::string& address, const ChunkSink& sink, const ProgressCallback& progress,
                                  const CancellationToken& cancel) {
    return route(address)->downloadFile(address, sink, progress, cancel);
}

Status SourceRouter::downloadFileFrom(const std::string& address, std::uint64_t offset, const ChunkSink& sink,
                                      const ProgressCallback& progress, const CancellationToken& cancel) {
    return route(address)->downloadFileFrom(address, offset, sink, progress, cancel);
}

Result<std::vector<RemoteEntry>> SourceRouter::listDirectory(const std::string& address) {
    return route(address)->listDirectory(address);
}

Status SourceRouter::uploadFile(const std::string& address, const ChunkReader& reader, std::uint64_t size,
                                const ProgressCallback& progress, const CancellationToken& cancel) {
    return route(address)->uploadFile(address, reader, size, progress, cancel);
}

}  // namespace rm
