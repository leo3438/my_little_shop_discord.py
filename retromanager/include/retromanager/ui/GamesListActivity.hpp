#pragma once

#include <borealis.hpp>
#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "retromanager/core/EventBus.hpp"
#include "retromanager/core/Result.hpp"
#include "retromanager/models/GameEntry.hpp"
#include "retromanager/services/DownloadQueueManager.hpp"
#include "retromanager/services/ShopService.hpp"
#include "retromanager/ui/ForwarderActivity.hpp"

namespace rm::ui {

// One game row: title + "region · year · size", and an "Installed" or
// "Queued" tag. On an installed game, X creates its HOME shortcut.
class GameCell : public brls::RecyclerCell {
  public:
    GameCell();
    static GameCell* create();

    void setState(bool installed, bool queued);

    std::string gameId;  // game currently bound to this (recycled) cell
    std::function<void()> onCreateForwarder;  // null when forwarders are unavailable

    BRLS_BIND(brls::Label, title, "game/title");
    BRLS_BIND(brls::Label, detail, "game/detail");
    BRLS_BIND(brls::Label, installedTag, "game/installed");

  private:
    bool installed_ = false;
};

// Shop contents, one section per system. Picking a game queues it (the
// download runs in the background; Y opens the downloads screen). Talks to
// services only: it never sees the transport, the parser nor the SD card.
class GamesListActivity : public brls::Activity {
  public:
    // `forwarders` null: no "Create a shortcut" action.
    GamesListActivity(ShopService& shop, DownloadQueueManager& downloads, EventBus& bus,
                      std::optional<ForwarderTools> forwarders = std::nullopt);
    ~GamesListActivity() override;

    CONTENT_FROM_XML_RES("activity/games_list.xml");

    void onContentAvailable() override;

  private:
    void load();
    void showListing(const ShopListing& listing);
    void onDownloadFinished(const DownloadFinished& event);
    void refreshTags();
    void showError(const Error& error);

    ShopService& shop_;
    DownloadQueueManager& downloads_;
    EventBus& bus_;
    std::optional<ForwarderTools> forwarders_;
    // Cleared on destruction: a load finishing after the user left the
    // screen must not touch the destroyed views. Both the destructor and
    // the callback run on the main thread, so a plain flag is enough.
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);

    // Shared with the data source; updated when a download completes.
    std::shared_ptr<std::set<std::string>> installed_ = std::make_shared<std::set<std::string>>();
    // Every cell the recycler created (it owns them): lets a finished
    // download refresh the visible row without reloadData(), which would
    // scroll the list back to the top.
    std::vector<GameCell*> cells_;
    EventBus::Subscription downloadFinished_, queueChanged_;

    BRLS_BIND(brls::Label, statusLabel, "games/status");
    BRLS_BIND(brls::Label, detailLabel, "games/detail");
    BRLS_BIND(brls::Label, motdLabel, "games/motd");
    BRLS_BIND(brls::RecyclerFrame, recycler, "games/recycler");
};

}  // namespace rm::ui
