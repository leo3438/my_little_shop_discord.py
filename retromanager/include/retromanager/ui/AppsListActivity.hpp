#pragma once

#include <borealis.hpp>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "retromanager/core/EventBus.hpp"
#include "retromanager/services/AppManager.hpp"
#include "retromanager/services/DownloadQueueManager.hpp"
#include "retromanager/services/ShopService.hpp"

namespace rm::ui {

// One homebrew row: title, "version · author · size", description, and an
// "Installed" / "Update" tag.
class AppCell : public brls::RecyclerCell {
  public:
    AppCell();
    static AppCell* create();

    void setState(AppState state, bool queued);

    std::string appId;  // app currently bound to this (recycled) cell

    BRLS_BIND(brls::Label, title, "app/title");
    BRLS_BIND(brls::Label, detail, "app/detail");
    BRLS_BIND(brls::Label, description, "app/description");
    BRLS_BIND(brls::Label, tag, "app/tag");
};

// "Emulators & homebrews": the shop's apps / emulators sections, one
// section each. Picking one queues its download into /switch/<name>/; the
// "Update N applications" button queues every app with an update. Y opens
// the downloads screen.
class AppsListActivity : public brls::Activity {
  public:
    AppsListActivity(ShopService& shop, AppManager& apps, DownloadQueueManager& downloads, EventBus& bus);
    ~AppsListActivity() override;

    CONTENT_FROM_XML_RES("activity/apps_list.xml");

    void onContentAvailable() override;

  private:
    void showApps(std::vector<AppEntry> apps);
    void onDownloadFinished(const DownloadFinished& event);
    void refreshTags();
    void updateAllButton();
    std::vector<AppEntry> updatable() const;  // UpdateAvailable and not queued yet

    ShopService& shop_;
    AppManager& apps_;
    DownloadQueueManager& downloads_;
    EventBus& bus_;
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
    std::vector<AppEntry> entries_;
    std::shared_ptr<std::map<std::string, AppState>> states_ = std::make_shared<std::map<std::string, AppState>>();
    std::vector<AppCell*> cells_;
    EventBus::Subscription downloadFinished_, queueChanged_;

    BRLS_BIND(brls::Button, updateAllButton_, "apps/update_all");
    BRLS_BIND(brls::Label, statusLabel, "games/status");
    BRLS_BIND(brls::Label, detailLabel, "games/detail");
    BRLS_BIND(brls::Label, motdLabel, "games/motd");
    BRLS_BIND(brls::RecyclerFrame, recycler, "games/recycler");
};

}  // namespace rm::ui
