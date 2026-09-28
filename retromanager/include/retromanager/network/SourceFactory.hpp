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

}  // namespace rm
