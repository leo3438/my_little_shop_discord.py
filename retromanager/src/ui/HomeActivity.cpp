#include "retromanager/ui/HomeActivity.hpp"

#include <utility>

#include "retromanager/ui/GamesListActivity.hpp"

namespace rm::ui {

HomeActivity::HomeActivity(AppContext& context, Status initStatus, ShopService& shop)
    : context_(context), initStatus_(std::move(initStatus)), shop_(shop) {}

void HomeActivity::onContentAvailable() {
    openShopButton->registerClickAction([this](brls::View*) {
        brls::Application::pushActivity(new GamesListActivity(shop_));
        return true;
    });

    platformLabel->setText(brls::getStr("retromanager/home/platform", context_.platformName()));
    sdRootLabel->setText(brls::getStr("retromanager/home/sd_root", context_.sdRootLabel()));
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
