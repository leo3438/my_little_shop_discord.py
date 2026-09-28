#pragma once

#include <borealis.hpp>
#include <memory>

#include "retromanager/core/EventBus.hpp"
#include "retromanager/core/Result.hpp"
#include "retromanager/models/GameEntry.hpp"
#include "retromanager/services/DownloadService.hpp"
#include "retromanager/services/ShopService.hpp"

namespace rm::ui {

// One game row: title + "system · region · size".
class GameCell : public brls::RecyclerCell {
  public:
    GameCell();
    static GameCell* create();

    BRLS_BIND(brls::Label, title, "game/title");
    BRLS_BIND(brls::Label, detail, "game/detail");
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
    void showIndex(const RepoIndex& index);
    void showError(const Error& error);

    ShopService& shop_;
    DownloadService& downloads_;
    EventBus& bus_;
    // Cleared on destruction: a load finishing after the user left the
    // screen must not touch the destroyed views. Both the destructor and
    // the callback run on the main thread, so a plain flag is enough.
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);

    BRLS_BIND(brls::Label, statusLabel, "games/status");
    BRLS_BIND(brls::Label, detailLabel, "games/detail");
    BRLS_BIND(brls::Label, motdLabel, "games/motd");
    BRLS_BIND(brls::RecyclerFrame, recycler, "games/recycler");
};

}  // namespace rm::ui
