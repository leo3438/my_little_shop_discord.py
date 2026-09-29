#pragma once

#include <borealis.hpp>
#include <functional>

#include "retromanager/core/AppContext.hpp"
#include "retromanager/core/Result.hpp"
#include "retromanager/core/EventBus.hpp"
#include "retromanager/services/AppManager.hpp"
#include "retromanager/services/BiosManager.hpp"
#include "retromanager/services/CloudSyncService.hpp"
#include "retromanager/services/DownloadQueueManager.hpp"
#include "retromanager/services/ShopService.hpp"

namespace rm::ui {

// Landing screen: platform and SD card status, entry points to the shop
// the cloud saves synchronization and the BIOS check.
// UI classes read state from AppContext / services and render it; they
// hold no business logic.
class HomeActivity : public brls::Activity {
  public:
    HomeActivity(AppContext& context, Status initStatus, ShopService& shop, DownloadQueueManager& downloads,
                 CloudSyncService& cloudSync, BiosManager& bios, AppManager& apps, EventBus& bus,
                 std::function<void()> openSources = nullptr);  // null: no Sources screen (built without curl)

    CONTENT_FROM_XML_RES("activity/home.xml");

    void onContentAvailable() override;

  private:
    void showQueueSize(std::size_t count);

    AppContext& context_;
    Status initStatus_;
    ShopService& shop_;
    DownloadQueueManager& downloads_;
    CloudSyncService& cloudSync_;
    BiosManager& bios_;
    AppManager& apps_;
    EventBus& bus_;
    EventBus::Subscription queueChanged_, sourcesChanged_;
    std::function<void()> openSources_;

    BRLS_BIND(brls::Button, openShopButton, "home/open_shop");
    BRLS_BIND(brls::Button, syncButton, "home/sync_saves");
    BRLS_BIND(brls::Button, biosButton, "home/bios");
    BRLS_BIND(brls::Button, appsButton, "home/apps");
    BRLS_BIND(brls::Button, downloadsButton, "home/downloads");
    BRLS_BIND(brls::Button, sourcesButton, "home/sources");
    BRLS_BIND(brls::Label, savesLabel, "home/saves");
    BRLS_BIND(brls::Label, platformLabel, "home/platform");
    BRLS_BIND(brls::Label, sdRootLabel, "home/sd_root");
    BRLS_BIND(brls::Label, shopLabel, "home/shop");
    BRLS_BIND(brls::Label, retroarchLabel, "home/retroarch");
    BRLS_BIND(brls::Label, versionLabel, "home/version");
};

}  // namespace rm::ui
