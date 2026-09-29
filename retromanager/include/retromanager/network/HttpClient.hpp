#pragma once

#include <cstddef>
#include <optional>
#include <string>

#include "retromanager/network/IRemoteSource.hpp"

namespace rm {

struct HttpConfig {
    std::string indexUrl;  // "https://example.org/retro/shop.json"
    std::string username;  // Basic auth; empty = anonymous
    std::string password;
    // On by default for the web (public certificates). A CA bundle (PEM)
    // can be given for platforms without one; empty = libcurl's default.
    bool verifyPeer = true;
    std::string caBundlePath;  // host path ("sdmc:/switch/RetroManager/cacert.pem" on Switch)
    long connectTimeoutSeconds = 10;
    long transferTimeoutSeconds = 60;  // whole-transfer limit for the (small) index only
    long stallTimeoutSeconds = 30;     // downloads: abort when no byte arrives for this long
    long maxRedirects = 5;
    std::size_t maxIndexBytes = 16 * 1024 * 1024;
};

// IRemoteSource over HTTP/HTTPS, backed by libcurl.
//
// - fetchIndex(): the index, into memory, size-capped.
// - downloadFile(): streamed into the caller's sink; downloadFileFrom()
//   resumes with "Range: bytes=<offset>-". A server that ignores it (200)
//   or refuses it (416) gives Unsupported before a single wrong byte
//   reaches the sink: the caller then starts over.
// - Error pages never reach the sink: the status is checked first (404 ->
//   NotFound, 401 -> AuthenticationFailed, 403 -> PermissionDenied...).
// - Any http(s) URL can be downloaded (an index on one site, files on
//   another), but credentials are only sent to the index's own scheme,
//   host and port, and never forwarded across redirects.
// - No directory listing nor upload over HTTP: Unsupported.
//
// Thread-safe: each call uses its own curl handle.
class HttpClient : public IRemoteSource {
  public:
    explicit HttpClient(HttpConfig config);

    static Status validate(const HttpConfig& config);
    // nullopt when `status` is the expected answer to a request from `offset`.
    static std::optional<Error> errorForStatus(long status, std::uint64_t offset);
    bool sendsCredentialsTo(const std::string& url) const;

    std::string describe() const override;
    std::string indexUrl() const override { return config_.indexUrl; }
    Result<std::string> fetchIndex() override;
    Status downloadFile(const std::string& url, const ChunkSink& sink, const ProgressCallback& progress,
                        const CancellationToken& cancel) override;
    Status downloadFileFrom(const std::string& url, std::uint64_t offset, const ChunkSink& sink,
                            const ProgressCallback& progress, const CancellationToken& cancel) override;
    Result<std::vector<RemoteEntry>> listDirectory(const std::string& url) override;
    Status uploadFile(const std::string& url, const ChunkReader& reader, std::uint64_t size,
                      const ProgressCallback& progress, const CancellationToken& cancel) override;

  private:
    Status transfer(const std::string& url, std::uint64_t offset, const ChunkSink& sink,
                    const ProgressCallback& progress, const CancellationToken& cancel, bool isIndex);

    HttpConfig config_;
};

}  // namespace rm
