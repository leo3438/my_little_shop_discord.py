#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "retromanager/network/IRemoteSource.hpp"

namespace rm {

struct FtpConfig {
    std::string host;                 // "nas.local" or "192.168.1.20"
    std::uint16_t port = 21;
    std::string username = "anonymous";
    std::string password;
    std::string indexPath = "/index.json";  // absolute path of the index on the server
    bool useTls = false;              // explicit FTPS (AUTH TLS)
    bool verifyPeer = true;           // TLS certificate verification
    long connectTimeoutSeconds = 10;
    long transferTimeoutSeconds = 60;   // whole-transfer limit for the (small) index only
    long stallTimeoutSeconds = 30;      // downloads: abort when no byte arrives for this long
    std::size_t maxIndexBytes = 16 * 1024 * 1024;  // protects the console's RAM
};

// IRemoteSource over FTP/FTPS, backed by libcurl (available on both desktop
// and Switch).
//
// - fetchIndex() / fetchFile(): small files, into memory, size-capped.
// - downloadFile(): streamed chunk by chunk (curl's ~256 KiB buffer) into
//   the caller's sink; nothing proportional to the file size is allocated.
// - Credentials are only ever sent to the configured host and port: a URL
//   pointing elsewhere is refused (PermissionDenied), so a malicious index
//   cannot harvest the NAS password.
//
// Thread-safe: each call uses its own curl handle.
class FtpClient : public IRemoteSource {
  public:
    explicit FtpClient(FtpConfig config);

    std::string describe() const override;
    std::string indexUrl() const override;
    Result<std::string> fetchIndex() override;
    Status downloadFile(const std::string& url, const ChunkSink& sink, const ProgressCallback& progress,
                        const CancellationToken& cancel) override;

    // Server path of `url` when it designates the configured server (same
    // host and port, ftp:// or ftps://), decoded. PermissionDenied otherwise.
    Result<std::string> pathOnServer(const std::string& url) const;

    // Downloads an arbitrary file (absolute server path) into memory.
    Result<std::string> fetchFile(std::string_view path, std::size_t maxBytes) const;

    // "ftp://host:port/escaped/path" (no credentials: they travel as curl
    // options, never inside URLs that could end up in logs).
    static Result<std::string> buildUrl(const FtpConfig& config, std::string_view path);

    static Status validate(const FtpConfig& config);

    static constexpr long kReceiveBufferBytes = 256 * 1024;

  private:
    FtpConfig config_;
};

}  // namespace rm
