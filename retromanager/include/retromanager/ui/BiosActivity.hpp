#pragma once

#include <borealis.hpp>
#include <map>
#include <memory>
#include <vector>

#include "retromanager/core/EventBus.hpp"
#include "retromanager/services/BiosManager.hpp"
#include "retromanager/services/ShopService.hpp"

namespace rm::ui {

// "BIOS check": one line per BIOS file RetroArch cores look for, grouped by
// system, with its state on the SD card. A on a missing file the shop
// provides downloads it; X downloads every missing one.
class BiosActivity : public brls::Activity {
  public:
    BiosActivity(BiosManager& bios, ShopService& shop, EventBus& bus);
    ~BiosActivity() override;

    CONTENT_FROM_XML_RES("activity/bios.xml");

    void onContentAvailable() override;

  private:
    void showRows(bool shopReachable);
    void updateSummary();
    void refreshCell(const BiosStatus& row);
    void install(std::vector<BiosEntry> offers);
    void onInstalled(const BiosInstalled& event);
    void onFinished(const BiosInstallFinished& event);
    std::vector<BiosEntry> missingOffers() const;

    BiosManager& bios_;
    ShopService& shop_;
    EventBus& bus_;
    std::vector<BiosEntry> offers_;
    std::vector<BiosStatus> rows_;
    bool shopReachable_ = false;
    std::map<std::string, brls::DetailCell*> cells_;  // by file name
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
    EventBus::Subscription installed_, finished_;

    BRLS_BIND(brls::Box, root, "bios/root");
    BRLS_BIND(brls::Label, folderLabel, "bios/folder");
    BRLS_BIND(brls::Label, statusLabel, "bios/status");
    BRLS_BIND(brls::ScrollingFrame, scroll, "bios/scroll");
    BRLS_BIND(brls::Box, list, "bios/list");
};

}  // namespace rm::ui
