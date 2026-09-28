#pragma once

#include <memory>

#include "retromanager/core/Result.hpp"
#include "retromanager/models/AppConfig.hpp"
#include "retromanager/network/FtpClient.hpp"
#include "retromanager/network/IRemoteSource.hpp"

namespace rm {

// ShopConfig -> FtpConfig. Pure: validates the URL and applies defaults
// (port 21, "index.json" for a directory URL, "ftps://" = explicit TLS).
Result<FtpConfig> ftpConfigFromShop(const ShopConfig& shop);

// Builds the source described by the configuration. Never returns null: a
// missing or invalid setting yields an UnavailableRemoteSource whose calls
// all fail with NotConfigured (the detailed reason in the message), so the
// UI shows it where the shop would be.
std::unique_ptr<IRemoteSource> createRemoteSource(const ShopConfig& shop);

// Where the cloud saves go.
struct SavesSource {
    std::unique_ptr<IRemoteSource> source;  // never null
    std::string baseUrl;                    // NAS folder, '/'-terminated; empty = cloud saves not configured
};

// From config.json's saves_url. Credentials: those in the URL
// ("ftp://user:pass@host/"), else the shop's when saves_url is on the same
// host and port as the shop, else anonymous: the NAS password is never sent
// to another server. With the demo shop ("type": "mock") and no saves_url,
// an in-memory demo NAS is used.
SavesSource createSavesSource(const AppConfig& config);

}  // namespace rm
