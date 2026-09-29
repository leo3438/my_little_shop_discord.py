#include "retromanager/ui/AppsListActivity.hpp"

#include <algorithm>
#include <cctype>
#include <utility>

#include "retromanager/core/Format.hpp"
#include "retromanager/ui/DownloadsActivity.hpp"

namespace rm::ui {

namespace {

std::string detailLine(const AppEntry& app) {
    std::string line;
    auto append = [&line](const std::string& part) {
        if (part.empty()) return;
        if (!line.empty()) line += "  ·  ";
        line += part;
    };
    if (!app.version.empty()) append(app.version.front() == 'v' ? app.version : "v" + app.version);
    if (!app.author.empty()) append(brls::getStr("retromanager/apps/by", app.author));
    if (app.sizeBytes > 0) append(formatBytes(app.sizeBytes));
    return line;
}

struct AppSection {
    std::string title;
    std::vector<AppEntry> apps;
};

class AppsDataSource : public brls::RecyclerDataSource {
  public:
    AppsDataSource(std::vector<AppSection> sections, std::shared_ptr<const std::map<std::string, AppState>> states,
                   AppManager& apps, DownloadQueueManager& downloads, EventBus& bus)
        : sections_(std::move(sections)), states_(std::move(states)), apps_(apps), downloads_(downloads), bus_(bus) {}

    int numberOfSections(brls::RecyclerFrame*) override { return static_cast<int>(sections_.size()); }
    int numberOfRows(brls::RecyclerFrame*, int section) override { return static_cast<int>(sections_[section].apps.size()); }
    std::string titleForHeader(brls::RecyclerFrame*, int section) override {
        return sections_[section].title + " (" + std::to_string(sections_[section].apps.size()) + ")";
    }
    float heightForHeader(brls::RecyclerFrame*, int) override { return 44; }

    brls::RecyclerCell* cellForRow(brls::RecyclerFrame* recycler, brls::IndexPath index) override {
        auto* cell = static_cast<AppCell*>(recycler->dequeueReusableCell("App"));
        const AppEntry& app = at(index);
        cell->appId = app.id;
        cell->title->setText(app.title);
        cell->detail->setText(detailLine(app));
        cell->description->setText(app.description);
        cell->description->setVisibility(app.description.empty() ? brls::Visibility::GONE : brls::Visibility::VISIBLE);
        auto state = states_->find(app.id);
        cell->setState(state != states_->end() ? state->second : AppState::NotInstalled, downloads_.isQueued(app.id));
        return cell;
    }

    void didSelectRowAt(brls::RecyclerFrame*, brls::IndexPath index) override {
        const AppEntry& app = at(index);
        brls::Logger::info("App queued: {} {} [{}] {}", app.title, app.version, app.id, app.nroUrl);
        downloads_.start(apps_.job(app));
        brls::Application::notify(brls::getStr("retromanager/downloads/queued", app.title));
    }

  private:
    const AppEntry& at(brls::IndexPath index) const {
        return sections_[index.section].apps[static_cast<std::size_t>(index.row)];
    }

    std::vector<AppSection> sections_;
    std::shared_ptr<const std::map<std::string, AppState>> states_;
    AppManager& apps_;
    DownloadQueueManager& downloads_;
    EventBus& bus_;
};

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

}  // namespace

AppCell::AppCell() { this->inflateFromXMLRes("xml/cells/app_cell.xml"); }

AppCell* AppCell::create() { return new AppCell(); }

void AppCell::setState(AppState state, bool queued) {
    if (queued) {
        tag->setText(brls::getStr("retromanager/downloads/tag_queued"));
    } else {
        switch (state) {
            case AppState::NotInstalled: tag->setVisibility(brls::Visibility::GONE); return;
            case AppState::Installed: tag->setText(brls::getStr("retromanager/shop/installed")); break;
            case AppState::UpdateAvailable: tag->setText(brls::getStr("retromanager/apps/update")); break;
        }
    }
    tag->setVisibility(brls::Visibility::VISIBLE);
}

AppsListActivity::AppsListActivity(ShopService& shop, AppManager& apps, DownloadQueueManager& downloads, EventBus& bus)
    : shop_(shop), apps_(apps), downloads_(downloads), bus_(bus) {}

AppsListActivity::~AppsListActivity() { *alive_ = false; }

void AppsListActivity::onContentAvailable() {
    recycler->estimatedRowHeight = 90;
    recycler->registerCell("App", [this]() {
        AppCell* cell = AppCell::create();
        cells_.push_back(cell);
        return cell;
    });
    downloadFinished_ = bus_.subscribe<DownloadFinished>([this](const DownloadFinished& e) { onDownloadFinished(e); });
    queueChanged_ = bus_.subscribe<DownloadQueueChanged>([this](const DownloadQueueChanged&) { refreshTags(); });
    getContentView()->registerAction(brls::getStr("retromanager/downloads/title"), brls::BUTTON_Y, [this](brls::View*) {
        brls::Application::pushActivity(new DownloadsActivity(downloads_, bus_));
        return true;
    });
    updateAllButton_->registerClickAction([this](brls::View*) {
        auto apps = updatable();
        for (const AppEntry& app : apps) downloads_.start(apps_.job(app));
        brls::Logger::info("App store: {} updates queued", apps.size());
        if (!apps.empty()) {
            brls::Application::notify(brls::getStr("retromanager/apps/updates_queued", std::to_string(apps.size())));
            brls::Application::giveFocus(recycler);  // the button disappears
        }
        return true;
    });

    statusLabel->setText(brls::getStr("retromanager/shop/loading", shop_.sourceDescription()));
    brls::Application::giveFocus(statusLabel);
    std::weak_ptr<bool> alive = alive_;
    shop_.loadIndexAsync([this, alive](Result<ShopListing> listing) {
        auto flag = alive.lock();
        if (!flag || !*flag) return;
        if (!listing.ok()) {
            brls::Logger::error("App store loading failed: {}", listing.error().describe());
            statusLabel->setText(describeForUser(listing.error()));
            detailLabel->setText(listing.error().message);
            detailLabel->setVisibility(brls::Visibility::VISIBLE);
            return;
        }
        for (const std::string& warning : listing.value().index.warnings) brls::Logger::warning("Shop index: {}", warning);
        showApps(listing.value().index.apps);
    });
}

void AppsListActivity::showApps(std::vector<AppEntry> apps) {
    entries_ = std::move(apps);
    if (entries_.empty()) {
        statusLabel->setText(brls::getStr("retromanager/apps/empty"));
        return;
    }
    *states_ = apps_.states(entries_);

    AppSection emulators{brls::getStr("retromanager/apps/emulators"), {}};
    AppSection homebrews{brls::getStr("retromanager/apps/homebrews"), {}};
    for (const AppEntry& app : entries_) (app.category == AppCategory::Emulator ? emulators : homebrews).apps.push_back(app);
    auto byTitle = [](const AppEntry& a, const AppEntry& b) {
        return std::lexicographical_compare(a.title.begin(), a.title.end(), b.title.begin(), b.title.end(),
                                            [](unsigned char x, unsigned char y) { return std::tolower(x) < std::tolower(y); });
    };
    std::sort(emulators.apps.begin(), emulators.apps.end(), byTitle);
    std::sort(homebrews.apps.begin(), homebrews.apps.end(), byTitle);
    std::vector<AppSection> sections;
    if (!emulators.apps.empty()) sections.push_back(std::move(emulators));
    if (!homebrews.apps.empty()) sections.push_back(std::move(homebrews));

    brls::Logger::info("App store: {} apps", entries_.size());
    statusLabel->setVisibility(brls::Visibility::GONE);
    recycler->setVisibility(brls::Visibility::VISIBLE);
    recycler->setDataSource(new AppsDataSource(std::move(sections), states_, apps_, downloads_, bus_));
    brls::Application::giveFocus(recycler);
    updateAllButton();
}

std::vector<AppEntry> AppsListActivity::updatable() const {
    std::vector<AppEntry> apps;
    for (const AppEntry& app : entries_) {
        auto state = states_->find(app.id);
        if (state != states_->end() && state->second == AppState::UpdateAvailable && !downloads_.isQueued(app.id)) {
            apps.push_back(app);
        }
    }
    return apps;
}

void AppsListActivity::updateAllButton() {
    std::size_t count = updatable().size();
    if (count == 0 && brls::Application::getCurrentFocus() == updateAllButton_) brls::Application::giveFocus(recycler);
    updateAllButton_->setText(brls::getStr(count == 1 ? "retromanager/apps/update_one" : "retromanager/apps/update_all",
                                           std::to_string(count)));
    updateAllButton_->setVisibility(count > 0 ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
}

void AppsListActivity::refreshTags() {
    if (entries_.empty()) return;
    for (AppCell* cell : cells_) {
        auto state = states_->find(cell->appId);
        if (state != states_->end()) cell->setState(state->second, downloads_.isQueued(cell->appId));
    }
    updateAllButton();
}

void AppsListActivity::onDownloadFinished(const DownloadFinished& event) {
    if (!event.result.ok() || entries_.empty()) return;
    *states_ = apps_.states(entries_);  // installed, or no longer an update
    refreshTags();
}

}  // namespace rm::ui
