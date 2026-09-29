#include "retromanager/ui/GamesListActivity.hpp"

#include <string>
#include <utility>
#include <vector>

#include "retromanager/core/Format.hpp"
#include "retromanager/ui/DownloadActivity.hpp"

namespace rm::ui {

namespace {

std::string detailLine(const GameEntry& game) {
    std::string line;
    auto append = [&line](const std::string& part) {
        if (part.empty()) return;
        if (!line.empty()) line += "  ·  ";
        line += part;
    };
    append(game.region);
    if (game.year) append(std::to_string(*game.year));
    if (game.sizeBytes > 0) append(formatBytes(game.sizeBytes));
    return line;
}

// Localized message for an error, with the technical details kept for logs.
std::string describeForUser(const Error& error) {
    switch (error.code) {
        case ErrorCode::NetworkError: return brls::getStr("retromanager/shop/error_network");
        case ErrorCode::AuthenticationFailed: return brls::getStr("retromanager/shop/error_auth");
        case ErrorCode::NotFound: return brls::getStr("retromanager/shop/error_not_found");
        case ErrorCode::ParseError:
        case ErrorCode::Unsupported: return brls::getStr("retromanager/shop/error_format");
        case ErrorCode::NotConfigured: return brls::getStr("retromanager/shop/error_not_configured");
        default: return brls::getStr("retromanager/shop/error_generic", error.describe());
    }
}

class GamesDataSource : public brls::RecyclerDataSource {
  public:
    GamesDataSource(std::vector<SystemSection> sections, std::shared_ptr<const std::set<std::string>> installed,
                    DownloadService& downloads, EventBus& bus)
        : sections_(std::move(sections)), installed_(std::move(installed)), downloads_(downloads), bus_(bus) {}

    int numberOfSections(brls::RecyclerFrame*) override { return static_cast<int>(sections_.size()); }

    int numberOfRows(brls::RecyclerFrame*, int section) override {
        return static_cast<int>(sections_[section].games.size());
    }

    std::string titleForHeader(brls::RecyclerFrame*, int section) override {
        const SystemSection& s = sections_[section];
        return s.displayName + " (" + std::to_string(s.games.size()) + ")";
    }

    // Every section is a system: show all headers, the first one included.
    float heightForHeader(brls::RecyclerFrame*, int) override { return 44; }

    brls::RecyclerCell* cellForRow(brls::RecyclerFrame* recycler, brls::IndexPath index) override {
        auto* cell = static_cast<GameCell*>(recycler->dequeueReusableCell("Game"));
        const GameEntry& game = at(index);
        cell->gameId = game.id;
        cell->title->setText(game.title);
        cell->detail->setText(detailLine(game));
        cell->setInstalled(installed_->count(game.id) > 0);
        return cell;
    }

    void didSelectRowAt(brls::RecyclerFrame*, brls::IndexPath index) override {
        const GameEntry& game = at(index);
        brls::Logger::info("Game selected: {} [{}] {}", game.title, game.id, game.romUrl);
        brls::Application::pushActivity(new DownloadActivity(downloads_, bus_, DownloadRequest::forGame(downloads_, game)));
    }

  private:
    const GameEntry& at(brls::IndexPath index) const {
        return sections_[index.section].games[static_cast<std::size_t>(index.row)];
    }

    std::vector<SystemSection> sections_;
    std::shared_ptr<const std::set<std::string>> installed_;
    DownloadService& downloads_;
    EventBus& bus_;
};

}  // namespace

GameCell::GameCell() { this->inflateFromXMLRes("xml/cells/game_cell.xml"); }

GameCell* GameCell::create() { return new GameCell(); }

void GameCell::setInstalled(bool installed) {
    installedTag->setVisibility(installed ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
}

GamesListActivity::GamesListActivity(ShopService& shop, DownloadService& downloads, EventBus& bus)
    : shop_(shop), downloads_(downloads), bus_(bus) {}

GamesListActivity::~GamesListActivity() { *alive_ = false; }

void GamesListActivity::onContentAvailable() {
    recycler->estimatedRowHeight = 70;
    recycler->registerCell("Game", [this]() {
        GameCell* cell = GameCell::create();
        cells_.push_back(cell);
        return cell;
    });
    downloadFinished_ = bus_.subscribe<DownloadFinished>([this](const DownloadFinished& e) { onDownloadFinished(e); });
    load();
}

void GamesListActivity::load() {
    statusLabel->setText(brls::getStr("retromanager/shop/loading", shop_.sourceDescription()));
    statusLabel->setVisibility(brls::Visibility::VISIBLE);
    recycler->setVisibility(brls::Visibility::GONE);
    brls::Application::giveFocus(statusLabel);

    std::weak_ptr<bool> alive = alive_;
    shop_.loadIndexAsync([this, alive](Result<ShopListing> listing) {
        auto flag = alive.lock();
        if (!flag || !*flag) return;  // screen closed while loading
        if (listing.ok()) {
            showListing(listing.value());
        } else {
            showError(listing.error());
        }
    });
}

void GamesListActivity::onDownloadFinished(const DownloadFinished& event) {
    if (!event.result.ok() || event.itemId.empty()) return;
    installed_->insert(event.itemId);
    for (GameCell* cell : cells_) {
        if (cell->gameId == event.itemId) cell->setInstalled(true);
    }
}

void GamesListActivity::showListing(const ShopListing& listing) {
    const RepoIndex& index = listing.index;
    *installed_ = listing.installedIds;
    for (const std::string& warning : index.warnings) brls::Logger::warning("Shop index: {}", warning);
    brls::Logger::info("Shop \"{}\": {} games, {} already installed", index.name, index.games.size(),
                       listing.installedIds.size());

    if (!index.motd.empty()) {
        motdLabel->setText(index.motd);
        motdLabel->setVisibility(brls::Visibility::VISIBLE);
    }
    if (index.games.empty()) {
        statusLabel->setText(brls::getStr("retromanager/shop/empty"));
        return;
    }

    statusLabel->setVisibility(brls::Visibility::GONE);
    recycler->setVisibility(brls::Visibility::VISIBLE);
    recycler->setDataSource(new GamesDataSource(ShopService::groupBySystem(index.games), installed_, downloads_, bus_));
    brls::Application::giveFocus(recycler);
}

void GamesListActivity::showError(const Error& error) {
    brls::Logger::error("Shop loading failed: {}", error.describe());
    statusLabel->setText(describeForUser(error));
    detailLabel->setText(error.message);
    detailLabel->setVisibility(brls::Visibility::VISIBLE);
}

}  // namespace rm::ui
