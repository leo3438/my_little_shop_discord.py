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
    long transferTimeoutSeconds = 60;
    std::size_t maxIndexBytes = 16 * 1024 * 1024;  // protects the console's RAM
};

// IRemoteSource over FTP/FTPS, backed by libcurl (available on both desktop
// and Switch). Phase 2 skeleton: downloads files into memory; streaming ROMs
// to the SD card comes with Phase 3.
//
// Thread-safe: each call uses its own curl handle.
class FtpClient : public IRemoteSource {
  public:
    explicit FtpClient(FtpConfig config);

    std::string describe() const override;
    std::string indexUrl() const override;
    Result<std::string> fetchIndex() override;

    // Downloads an arbitrary file (absolute server path) into memory.
    Result<std::string> fetchFile(std::string_view path, std::size_t maxBytes) const;

    // "ftp://host:port/escaped/path" (no credentials: they travel as curl
    // options, never inside URLs that could end up in logs).
    static Result<std::string> buildUrl(const FtpConfig& config, std::string_view path);

    static Status validate(const FtpConfig& config);

  private:
    FtpConfig config_;
};

}  // namespace rm
