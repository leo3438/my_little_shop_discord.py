#pragma once

#include <borealis.hpp>

#include "retromanager/core/AppContext.hpp"
#include "retromanager/core/Result.hpp"
#include "retromanager/services/ShopService.hpp"

namespace rm::ui {

// Landing screen: platform and SD card status, entry point to the shop.
// UI classes read state from AppContext / services and render it; they
// hold no business logic.
class HomeActivity : public brls::Activity {
  public:
    HomeActivity(AppContext& context, Status initStatus, ShopService& shop);

    CONTENT_FROM_XML_RES("activity/home.xml");

    void onContentAvailable() override;

  private:
    AppContext& context_;
    Status initStatus_;
    ShopService& shop_;

    BRLS_BIND(brls::Button, openShopButton, "home/open_shop");
    BRLS_BIND(brls::Label, platformLabel, "home/platform");
    BRLS_BIND(brls::Label, sdRootLabel, "home/sd_root");
    BRLS_BIND(brls::Label, retroarchLabel, "home/retroarch");
    BRLS_BIND(brls::Label, versionLabel, "home/version");
};

}  // namespace rm::ui
