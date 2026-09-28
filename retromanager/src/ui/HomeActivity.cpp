#include "retromanager/ui/HomeActivity.hpp"

#include <utility>

#include "retromanager/ui/GamesListActivity.hpp"
#include "retromanager/ui/SyncActivity.hpp"

namespace rm::ui {

HomeActivity::HomeActivity(AppContext& context, Status initStatus, ShopService& shop, DownloadService& downloads,
                           CloudSyncService& cloudSync, EventBus& bus)
    : context_(context),
      initStatus_(std::move(initStatus)),
      shop_(shop),
      downloads_(downloads),
      cloudSync_(cloudSync),
      bus_(bus) {}

void HomeActivity::onContentAvailable() {
    openShopButton->registerClickAction([this](brls::View*) {
        brls::Application::pushActivity(new GamesListActivity(shop_, downloads_, bus_));
        return true;
    });
    syncButton->registerClickAction([this](brls::View*) {
        brls::Application::pushActivity(new SyncActivity(cloudSync_, bus_));
        return true;
    });

    platformLabel->setText(brls::getStr("retromanager/home/platform", context_.platformName()));
    sdRootLabel->setText(brls::getStr("retromanager/home/sd_root", context_.sdRootLabel()));
    shopLabel->setText(brls::getStr("retromanager/home/shop", shop_.sourceDescription()));
    savesLabel->setText(cloudSync_.isConfigured()
                            ? brls::getStr("retromanager/home/saves", cloudSync_.remoteBaseUrl())
                            : brls::getStr("retromanager/home/saves_not_configured"));
    versionLabel->setText(brls::getStr("retromanager/home/version", RM_VERSION));

    if (!initStatus_) {
        retroarchLabel->setText(brls::getStr("retromanager/home/init_failed", initStatus_.error().describe()));
    } else if (context_.isRetroArchInstalled()) {
        retroarchLabel->setText(brls::getStr("retromanager/home/retroarch_found"));
    } else {
        retroarchLabel->setText(brls::getStr("retromanager/home/retroarch_missing"));
    }
}

}  // namespace rm::ui
