#pragma once

#include <borealis.hpp>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "retromanager/core/EventBus.hpp"
#include "retromanager/core/Result.hpp"
#include "retromanager/models/GameEntry.hpp"
#include "retromanager/services/DownloadService.hpp"
#include "retromanager/services/ShopService.hpp"

namespace rm::ui {

// One game row: title + "region · year · size", and an "Installed" tag.
class GameCell : public brls::RecyclerCell {
  public:
    GameCell();
    static GameCell* create();

    void setInstalled(bool installed);

    std::string gameId;  // game currently bound to this (recycled) cell

    BRLS_BIND(brls::Label, title, "game/title");
    BRLS_BIND(brls::Label, detail, "game/detail");
    BRLS_BIND(brls::Label, installedTag, "game/installed");
};

// Shop contents, one section per system. Talks to services only (ShopService
// for the index, DownloadService when a game is picked): it never sees the
// transport, the parser nor the SD card.
class GamesListActivity : public brls::Activity {
  public:
    GamesListActivity(ShopService& shop, DownloadService& downloads, EventBus& bus);
    ~GamesListActivity() override;

    CONTENT_FROM_XML_RES("activity/games_list.xml");

    void onContentAvailable() override;

  private:
    void load();
    void showListing(const ShopListing& listing);
    void onDownloadFinished(const DownloadFinished& event);
    void showError(const Error& error);

    ShopService& shop_;
    DownloadService& downloads_;
    EventBus& bus_;
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
    EventBus::Subscription downloadFinished_;

    BRLS_BIND(brls::Label, statusLabel, "games/status");
    BRLS_BIND(brls::Label, detailLabel, "games/detail");
    BRLS_BIND(brls::Label, motdLabel, "games/motd");
    BRLS_BIND(brls::RecyclerFrame, recycler, "games/recycler");
};

}  // namespace rm::ui
