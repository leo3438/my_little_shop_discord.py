#pragma once

#include <borealis.hpp>

#include "retromanager/core/AppContext.hpp"
#include "retromanager/core/Result.hpp"

namespace rm::ui {

// Landing screen. Phase 1: shows the platform and SD card status only.
// UI classes read state from AppContext / services and render it; they
// hold no business logic.
class HomeActivity : public brls::Activity {
  public:
    HomeActivity(AppContext& context, Status initStatus);

    CONTENT_FROM_XML_RES("activity/home.xml");

    void onContentAvailable() override;

  private:
    AppContext& context_;
    Status initStatus_;

    BRLS_BIND(brls::Label, platformLabel, "home/platform");
    BRLS_BIND(brls::Label, sdRootLabel, "home/sd_root");
    BRLS_BIND(brls::Label, retroarchLabel, "home/retroarch");
    BRLS_BIND(brls::Label, versionLabel, "home/version");
};

}  // namespace rm::ui
