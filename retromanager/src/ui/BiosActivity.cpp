#include "retromanager/ui/BiosActivity.hpp"

#include <utility>

#include "retromanager/models/Systems.hpp"

namespace rm::ui {

namespace {

const NVGcolor kOk = nvgRGB(46, 204, 113);
const NVGcolor kWarning = nvgRGB(241, 196, 15);
const NVGcolor kMissing = nvgRGB(231, 76, 60);

std::string systemTitle(const std::string& system) {
    return system.empty() ? brls::getStr("retromanager/bios/other_system") : systems::displayName(system);
}

}  // namespace

BiosActivity::BiosActivity(BiosManager& bios, ShopService& shop, EventBus& bus) : bios_(bios), shop_(shop), bus_(bus) {}

BiosActivity::~BiosActivity() {
    *alive_ = false;
    bios_.cancel();  // leaving the screen stops after the current file
}

void BiosActivity::onContentAvailable() {
    folderLabel->setText(brls::getStr("retromanager/bios/folder", bios_.systemDirectory()));
    statusLabel->setText(brls::getStr("retromanager/bios/loading", shop_.sourceDescription()));
    brls::Application::giveFocus(statusLabel);

    installed_ = bus_.subscribe<BiosInstalled>([this](const BiosInstalled& e) { onInstalled(e); });
    finished_ = bus_.subscribe<BiosInstallFinished>([this](const BiosInstallFinished& e) { onFinished(e); });

    // The shop tells which files it can provide; the SD card state is shown
    // either way.
    std::weak_ptr<bool> alive = alive_;
    shop_.loadIndexAsync([this, alive](Result<ShopListing> listing) {
        auto flag = alive.lock();
        if (!flag || !*flag) return;
        if (listing.ok()) {
            offers_ = listing.value().index.bios;
        } else {
            brls::Logger::warning("BIOS: shop unavailable: {}", listing.error().describe());
        }
        showRows(listing.ok());
    });
}

void BiosActivity::updateSummary() {
    int missing = 0;
    int available = 0;
    for (const BiosStatus& row : rows_) {
        if (row.state != BiosState::Missing) continue;
        ++missing;
        if (row.offer) ++available;
    }
    std::string status = missing == 0 ? brls::getStr("retromanager/bios/all_present")
                         : available == 0
                             ? brls::getStr("retromanager/bios/summary_none", std::to_string(missing))
                             : brls::getStr("retromanager/bios/summary", std::to_string(missing), std::to_string(available));
    if (!shopReachable_) status += "\n" + brls::getStr("retromanager/bios/shop_unavailable");
    statusLabel->setText(status);
    root->setActionAvailable(brls::BUTTON_X, available > 0 && !bios_.running());
}

void BiosActivity::showRows(bool shopReachable) {
    shopReachable_ = shopReachable;
    rows_ = bios_.check(offers_);
    statusLabel->setFocusable(false);

    std::string currentSystem = "\x01";  // no system has this id
    brls::View* first = nullptr;
    for (const BiosStatus& row : rows_) {
        if (row.system != currentSystem) {
            currentSystem = row.system;
            auto* header = new brls::Header();
            header->setTitle(systemTitle(row.system));
            list->addView(header);
        }
        auto* cell = new brls::DetailCell();
        std::string title = row.fileName;
        if (!row.description.empty()) title += "  ·  " + row.description;
        if (row.required) title += "  ·  " + brls::getStr("retromanager/bios/required");
        cell->setText(title);
        const std::string fileName = row.fileName;
        cell->registerClickAction([this, fileName](brls::View*) {
            for (const BiosStatus& r : rows_) {
                if (r.fileName == fileName && r.offer && r.state != BiosState::Ok && !bios_.running()) {
                    install({*r.offer});
                }
            }
            return true;
        });
        cells_[row.fileName] = cell;
        list->addView(cell);
        refreshCell(row);
        if (first == nullptr) first = cell;
    }

    root->registerAction(brls::getStr("retromanager/bios/download_all"), brls::BUTTON_X, [this](brls::View*) {
        if (!bios_.running()) {
            auto offers = missingOffers();
            if (!offers.empty()) install(std::move(offers));
        }
        return true;
    });
    updateSummary();
    if (first != nullptr) brls::Application::giveFocus(first);
}

void BiosActivity::refreshCell(const BiosStatus& row) {
    auto it = cells_.find(row.fileName);
    if (it == cells_.end()) return;
    brls::DetailCell* cell = it->second;
    switch (row.state) {
        case BiosState::Ok:
            cell->setDetailText(brls::getStr("retromanager/bios/state_ok"));
            cell->setDetailTextColor(kOk);
            break;
        case BiosState::Unrecognized:
            cell->setDetailText(brls::getStr(row.offer ? "retromanager/bios/state_unrecognized_offer"
                                                       : "retromanager/bios/state_unrecognized"));
            cell->setDetailTextColor(kWarning);
            break;
        case BiosState::Unverified:
            cell->setDetailText(brls::getStr("retromanager/bios/state_unverified"));
            cell->setDetailTextColor(kOk);
            break;
        case BiosState::Missing:
            cell->setDetailText(brls::getStr(row.offer ? "retromanager/bios/state_missing_offer"
                                                       : "retromanager/bios/state_missing"));
            cell->setDetailTextColor(kMissing);
            break;
    }
}

std::vector<BiosEntry> BiosActivity::missingOffers() const {
    std::vector<BiosEntry> offers;
    for (const BiosStatus& row : rows_) {
        if (row.state == BiosState::Missing && row.offer) offers.push_back(*row.offer);
    }
    return offers;
}

void BiosActivity::install(std::vector<BiosEntry> offers) {
    for (const BiosEntry& offer : offers) {
        auto it = cells_.find(offer.fileName);
        if (it == cells_.end()) continue;
        it->second->setDetailText(brls::getStr("retromanager/bios/downloading"));
        it->second->setDetailTextColor(brls::Application::getTheme().getColor("brls/text_disabled"));
    }
    root->setActionAvailable(brls::BUTTON_X, false);
    bios_.startInstall(std::move(offers));
}

void BiosActivity::onInstalled(const BiosInstalled& event) {
    if (event.result.ok()) {
        brls::Logger::info("BIOS installed: {}", event.fileName);
        return;  // final state shown by onFinished() after a re-check
    }
    brls::Logger::error("BIOS {}: {}", event.fileName, event.result.error().describe());
    auto it = cells_.find(event.fileName);
    if (it == cells_.end()) return;
    it->second->setDetailText(brls::getStr(event.result.error().code == ErrorCode::IntegrityError
                                               ? "retromanager/bios/failed_integrity"
                                               : "retromanager/bios/failed"));
    it->second->setDetailTextColor(kMissing);
}

void BiosActivity::onFinished(const BiosInstallFinished& event) {
    // Same offers, so the same rows: cells are updated in place.
    std::vector<BiosStatus> fresh = bios_.check(offers_);
    for (const BiosStatus& row : fresh) {
        bool failedNow = false;
        for (const BiosStatus& old : rows_) {
            if (old.fileName == row.fileName && row.state == BiosState::Missing && old.state == BiosState::Missing) {
                failedNow = true;  // keep the failure message on screen
            }
        }
        if (!failedNow) refreshCell(row);
    }
    rows_ = std::move(fresh);
    updateSummary();
    brls::Application::notify(brls::getStr("retromanager/bios/installed", std::to_string(event.installed)));
}

}  // namespace rm::ui
