#pragma once

#include <string>

#include "retromanager/core/Result.hpp"
#include "retromanager/models/AppConfig.hpp"
#include "retromanager/network/SourceFactory.hpp"
#include "retromanager/network/SourceRouter.hpp"
#include "retromanager/services/ConfigManager.hpp"

namespace rm {

// Published by the Sources screen after a change (the home screen updates
// its "Shop:" line).
struct SourcesChanged {};

// The user's shops, edited at run time: every change is applied to the
// router (usable immediately, no restart) and saved to config.json.
class SourceCatalog {
  public:
    // `configLoaded` false: config.json could not be read; it is then never
    // rewritten (the user's file, possibly just a typo away from valid, is
    // left alone) and edits are refused.
    SourceCatalog(AppConfig& config, SourceRouter& router, ConfigManager& manager, bool configLoaded);

    // Validates (unique name, usable ftp(s):// or http(s):// URL), creates
    // the client, saves. `type` empty = from the URL; verifyTls as given,
    // or the type's default when `type` was empty.
    Status add(ShopConfig source);
    Status remove(const std::string& name);
    Status activate(const std::string& name);

    const AppConfig& config() const { return config_; }

  private:
    Status save();

    AppConfig& config_;
    SourceRouter& router_;
    ConfigManager& manager_;
    bool writable_;
};

}  // namespace rm
