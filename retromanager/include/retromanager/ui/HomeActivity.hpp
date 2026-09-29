#pragma once

#include <borealis.hpp>

#include "retromanager/core/AppContext.hpp"
#include "retromanager/core/Result.hpp"
#include "retromanager/core/EventBus.hpp"
#include "retromanager/services/AppManager.hpp"
#include "retromanager/services/BiosManager.hpp"
#include "retromanager/services/CloudSyncService.hpp"
#include "retromanager/services/DownloadService.hpp"
#include "retromanager/services/ShopService.hpp"

namespace rm::ui {

// Landing screen: platform and SD card status, entry points to the shop
// the cloud saves synchronization and the BIOS check.
// UI classes read state from AppContext / services and render it; they
// hold no business logic.
class HomeActivity : public brls::Activity {
  public:
    HomeActivity(AppContext& context, Status initStatus, ShopService& shop, DownloadService& downloads,
                 CloudSyncService& cloudSync, BiosManager& bios, AppManager& apps, EventBus& bus);

    CONTENT_FROM_XML_RES("activity/home.xml");

    void onContentAvailable() override;

  private:
    AppContext& context_;
    Status initStatus_;
    ShopService& shop_;
    DownloadService& downloads_;
    CloudSyncService& cloudSync_;
    BiosManager& bios_;
    AppManager& apps_;
    EventBus& bus_;

    BRLS_BIND(brls::Button, openShopButton, "home/open_shop");
    BRLS_BIND(brls::Button, syncButton, "home/sync_saves");
    BRLS_BIND(brls::Button, biosButton, "home/bios");
    BRLS_BIND(brls::Button, appsButton, "home/apps");
    BRLS_BIND(brls::Label, savesLabel, "home/saves");
    BRLS_BIND(brls::Label, platformLabel, "home/platform");
    BRLS_BIND(brls::Label, sdRootLabel, "home/sd_root");
    BRLS_BIND(brls::Label, shopLabel, "home/shop");
    BRLS_BIND(brls::Label, retroarchLabel, "home/retroarch");
    BRLS_BIND(brls::Label, versionLabel, "home/version");
};

}  // namespace rm::ui
