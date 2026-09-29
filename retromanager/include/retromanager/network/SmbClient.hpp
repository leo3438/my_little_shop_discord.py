#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "retromanager/network/IRemoteSource.hpp"

namespace rm {

struct SmbConfig {
    std::string host;                       // "192.168.1.102", "zimaos.local"
    std::uint16_t port = 445;
    std::string share;                      // "HDD-Storage1"
    std::string indexPath = "/index.json";  // inside the share, starts with '/'
    // Empty user name: guest access (public share). The domain is usually
    // empty (workgroup); "DOMAIN;user" in a URL sets it too.
    std::string username;
    std::string password;
    std::string domain;
    int timeoutSeconds = 15;                // connection and each request
    std::size_t maxIndexBytes = 16 * 1024 * 1024;
};

// IRemoteSource over SMB2/SMB3 (Windows shares, Samba NAS: ZimaOS, Synology,
// TrueNAS...), backed by libsmb2. libcurl's own smb:// only speaks SMB1,
// which Samba disables by default, hence this separate client.
//
// URLs: smb://host[:port]/share/path/file, e.g.
// smb://192.168.1.102/HDD-Storage1/roms%20ds/shop.json (raw spaces are
// accepted too). Authentication is NTLM with the configured user and
// password, never taken from index URLs; an empty user name connects as
// guest. Like FtpClient, it only talks to its own host, port and share: any
// other URL is refused (PermissionDenied).
//
// Downloads stream in chunks of the server's maximum read size (capped at
// 1 MiB); resume seeks to the offset. Uploads go to ".<name>.tmp" then are
// renamed. Thread-safe: every call opens its own connection.
class SmbClient : public IRemoteSource {
  public:
    explicit SmbClient(SmbConfig config);

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

    // Path inside the share ("/roms ds/game.nds", decoded) when `url` is on
    // this client's host, port and share; PermissionDenied otherwise.
    Result<std::string> pathOnServer(const std::string& url) const;

    // "smb://host:port/share/escaped/path", no credentials.
    static std::string buildUrl(const SmbConfig& config, std::string_view path);

    static Status validate(const SmbConfig& config);

    static constexpr std::uint32_t kMaxChunkBytes = 1024 * 1024;

  private:
    SmbConfig config_;
};

// smb://[domain;][user[:password]@]host[:port]/share[/path] -> SmbConfig. The
// dedicated user / password win over the URL's. A URL ending with '/' means
// "<url>index.json". NotConfigured when empty, InvalidArgument without a
// share, Unsupported for another scheme.
Result<SmbConfig> smbConfigFromUrl(const std::string& url, const std::string& username, const std::string& password);

}  // namespace rm
