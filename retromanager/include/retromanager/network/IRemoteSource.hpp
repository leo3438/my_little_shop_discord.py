#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "retromanager/core/Cancellation.hpp"
#include "retromanager/core/Result.hpp"

namespace rm {

struct TransferProgress {
    std::uint64_t received = 0;
    std::uint64_t total = 0;  // 0 when the server does not announce a size
};

// Receives downloaded bytes in order. Returning an error aborts the
// transfer, and downloadFile() returns that same error.
using ChunkSink = std::function<Status(const char* data, std::size_t size)>;
// Called from the transferring thread, possibly very often: keep it cheap.
using ProgressCallback = std::function<void(const TransferProgress&)>;
// Supplies bytes to upload: fills at most `capacity` bytes, returns how many
// (0 = end of data). Returning an error aborts the upload.
using ChunkReader = std::function<Result<std::size_t>(char* buffer, std::size_t capacity)>;

struct RemoteEntry {
    std::string name;  // bare name
    bool isDirectory = false;
    std::uint64_t size = 0;
    std::optional<std::int64_t> modifiedAt;  // Unix seconds, UTC, when the server tells
};

// A place a shop lives: an FTP share on a NAS, an HTTP server, an SMB share,
// or an in-memory mock.
//
// The network layer only moves bytes. It knows nothing about the index
// format (RepoIndexParser) nor about the SD card (RomStore): downloads are
// pushed chunk by chunk into a ChunkSink, so a ROM is never held in memory.
//
// Implementations are called from background threads and must not touch
// the UI. Each call is independent (a connection per call is fine).
class IRemoteSource {
  public:
    virtual ~IRemoteSource() = default;

    // Human readable location, safe to display and log (never contains a
    // password), e.g. "ftp://leo@nas.local:21/shop/index.json".
    virtual std::string describe() const = 0;

    // Absolute URL of the index, used to resolve relative entries. Empty
    // when the source has no meaningful URL.
    virtual std::string indexUrl() const = 0;

    // Downloads the raw shop index document (small, bounded size).
    virtual Result<std::string> fetchIndex() = 0;

    // Streams `url` (an absolute URL taken from the index) into `sink`.
    // Fails with Cancelled as soon as `cancel` is set. `progress` may be
    // empty. Sources refuse URLs they must not send their credentials to.
    virtual Status downloadFile(const std::string& url, const ChunkSink& sink, const ProgressCallback& progress,
                                const CancellationToken& cancel) = 0;

    // Resumes a download: only the bytes from `offset` on reach `sink`
    // (FTP: REST <offset> before RETR). Progress counts the whole file,
    // offset included. Unsupported when the source or the server cannot
    // resume (or `offset` is past the end): the caller then starts over.
    // Default: no resume support.
    virtual Status downloadFileFrom(const std::string& url, std::uint64_t offset, const ChunkSink& sink,
                                    const ProgressCallback& progress, const CancellationToken& cancel) {
        if (offset == 0) return downloadFile(url, sink, progress, cancel);
        return makeError(ErrorCode::Unsupported, "this source cannot resume downloads");
    }

    // Entries of the directory at `url` (names only, no "." / ".."), in
    // server order. NotFound when the directory does not exist.
    virtual Result<std::vector<RemoteEntry>> listDirectory(const std::string& url) = 0;

    // Streams `size` bytes from `reader` to `url`. Missing parent directories
    // are created. Atomic on the remote side: the file appears under its
    // final name only once complete, so an interrupted upload never replaces
    // a good remote file with a truncated one.
    virtual Status uploadFile(const std::string& url, const ChunkReader& reader, std::uint64_t size,
                              const ProgressCallback& progress, const CancellationToken& cancel) = 0;
};

// Stand-in for a source that could not be created (missing or invalid
// configuration): every call reports the same error.
class UnavailableRemoteSource : public IRemoteSource {
  public:
    UnavailableRemoteSource(Error error, std::string description)
        : error_(std::move(error)), description_(std::move(description)) {}

    std::string describe() const override { return description_; }
    std::string indexUrl() const override { return ""; }
    Result<std::string> fetchIndex() override { return error_; }
    Status downloadFile(const std::string&, const ChunkSink&, const ProgressCallback&,
                        const CancellationToken&) override {
        return error_;
    }
    Result<std::vector<RemoteEntry>> listDirectory(const std::string&) override { return error_; }
    Status uploadFile(const std::string&, const ChunkReader&, std::uint64_t, const ProgressCallback&,
                      const CancellationToken&) override {
        return error_;
    }

    const Error& error() const { return error_; }

  private:
    Error error_;
    std::string description_;
};

}  // namespace rm
