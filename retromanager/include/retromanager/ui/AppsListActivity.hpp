#pragma once

#include <borealis.hpp>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "retromanager/core/EventBus.hpp"
#include "retromanager/services/AppManager.hpp"
#include "retromanager/services/DownloadService.hpp"
#include "retromanager/services/ShopService.hpp"

namespace rm::ui {

// One homebrew row: title, "version · author · size", description, and an
// "Installed" / "Update" tag.
class AppCell : public brls::RecyclerCell {
  public:
    AppCell();
    static AppCell* create();

    void setState(AppState state);

    std::string appId;  // app currently bound to this (recycled) cell

    BRLS_BIND(brls::Label, title, "app/title");
    BRLS_BIND(brls::Label, detail, "app/detail");
    BRLS_BIND(brls::Label, description, "app/description");
    BRLS_BIND(brls::Label, tag, "app/tag");
};

// "Emulators & homebrews": the shop's apps / emulators sections, one
// section each. Picking one downloads it (DownloadActivity) into
// /switch/<name>/.
class AppsListActivity : public brls::Activity {
  public:
    AppsListActivity(ShopService& shop, AppManager& apps, DownloadService& downloads, EventBus& bus);
    ~AppsListActivity() override;

    CONTENT_FROM_XML_RES("activity/apps_list.xml");

    void onContentAvailable() override;

  private:
    void showApps(std::vector<AppEntry> apps);
    void onDownloadFinished(const DownloadFinished& event);

    ShopService& shop_;
    AppManager& apps_;
    DownloadService& downloads_;
    EventBus& bus_;
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
    std::vector<AppEntry> entries_;
    std::shared_ptr<std::map<std::string, AppState>> states_ = std::make_shared<std::map<std::string, AppState>>();
    std::vector<AppCell*> cells_;
    EventBus::Subscription downloadFinished_;

    BRLS_BIND(brls::Label, statusLabel, "games/status");
    BRLS_BIND(brls::Label, detailLabel, "games/detail");
    BRLS_BIND(brls::Label, motdLabel, "games/motd");
    BRLS_BIND(brls::RecyclerFrame, recycler, "games/recycler");
};

}  // namespace rm::ui
