#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "retromanager/network/IRemoteSource.hpp"

namespace rm {

struct SourceInfo {
    std::string name;
    std::string type;         // "ftp", "http", "mock"
    std::string description;  // safe to display (no password)
    bool active = false;
};

// All the configured shops behind one IRemoteSource.
//
// - The shop side (fetchIndex, describe, indexUrl) is the *active* source:
//   the one the shop screen shows, switchable at run time.
// - Every URL call (downloads, listings, uploads) goes to the source that
//   owns the URL: same server (scheme family, host, port) as its index. A
//   queued download therefore keeps using its own server and credentials
//   after the user switches shops.
// - Other http(s) URLs (box art from thumbnails.libretro.com...) go to the
//   anonymous public web client, if any; other FTP servers are refused
//   (PermissionDenied), so an index cannot make the console contact them.
//
// Thread-safe. A removed source stays alive as long as a transfer uses it.
class SourceRouter : public IRemoteSource {
  public:
    explicit SourceRouter(std::shared_ptr<IRemoteSource> publicWeb = nullptr);

    // False when the name is taken (names ignore case). The first source
    // added becomes the active one.
    bool add(std::string name, std::string type, std::shared_ptr<IRemoteSource> source);
    bool remove(const std::string& name);
    bool setActive(const std::string& name);
    std::string activeName() const;  // "" when there is no source
    std::vector<SourceInfo> sources() const;

    // The source a URL belongs to; an Unavailable one when none.
    std::shared_ptr<IRemoteSource> route(const std::string& url) const;

    std::string describe() const override;
    std::string indexUrl() const override;
    Result<std::string> fetchIndex() override;
    Status downloadFile(const std::string& url, const ChunkSink& sink, const ProgressCallback& progress,
                        const CancellationToken& cancel) override;
    Status downloadFileFrom(const std::string& url, std::uint64_t offset, const ChunkSink& sink,
                            const ProgressCallback& progress, const CancellationToken& cancel) override;
    Result<std::vector<RemoteEntry>> listDirectory(const std::string& url) override;
    Status uploadFile(const std::string& url, const ChunkReader& reader, std::uint64_t size,
                      const ProgressCallback& progress, const CancellationToken& cancel) override;

  private:
    struct Entry {
        std::string name;
        std::string type;
        std::shared_ptr<IRemoteSource> source;
    };

    std::shared_ptr<IRemoteSource> active() const;

    mutable std::mutex mutex_;
    std::vector<Entry> entries_;
    std::string active_;
    std::shared_ptr<IRemoteSource> publicWeb_;
};

}  // namespace rm
