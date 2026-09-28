#include "retromanager/services/ShopService.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <memory>

#include "retromanager/models/Systems.hpp"
#include "retromanager/parsers/RepoIndexParser.hpp"

namespace rm {

namespace {

std::string foldCase(const std::string& value) {
    std::string out(value);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

}  // namespace

ShopService::ShopService(IRemoteSource& source, ITaskRunner& tasks, RomStore* store)
    : source_(source), tasks_(tasks), store_(store) {}

Result<RepoIndex> ShopService::loadIndex() {
    auto document = source_.fetchIndex();
    if (!document) return document.error();
    return RepoIndexParser(source_.indexUrl()).parse(document.value());
}

Result<ShopListing> ShopService::loadListing() {
    auto index = loadIndex();
    if (!index) return index.error();
    ShopListing listing;
    listing.index = std::move(index.value());
    if (store_ != nullptr) {
        for (const GameEntry& game : listing.index.games) {
            if (store_->isInstalled(game)) listing.installedIds.insert(game.id);
        }
    }
    return listing;
}

void ShopService::loadIndexAsync(ListingCallback onDone) {
    tasks_.runInBackground([this, onDone = std::move(onDone)]() mutable {
        // Result has no default constructor: carry it through a shared_ptr
        // so the main-thread task stays copyable (std::function requirement).
        auto result = std::make_shared<Result<ShopListing>>(loadListing());
        tasks_.runOnMainThread([onDone = std::move(onDone), result]() { onDone(std::move(*result)); });
    });
}

std::vector<SystemSection> ShopService::groupBySystem(const std::vector<GameEntry>& games) {
    std::map<std::string, SystemSection> bySystem;
    for (const GameEntry& game : games) {
        SystemSection& section = bySystem[game.system];
        if (section.system.empty()) {
            section.system = game.system;
            section.displayName = systems::displayName(game.system);
        }
        section.games.push_back(game);
    }

    std::vector<SystemSection> sections;
    sections.reserve(bySystem.size());
    for (auto& entry : bySystem) {
        SystemSection& section = entry.second;
        std::stable_sort(section.games.begin(), section.games.end(), [](const GameEntry& a, const GameEntry& b) {
            return foldCase(a.title) < foldCase(b.title);
        });
        sections.push_back(std::move(section));
    }

    std::stable_sort(sections.begin(), sections.end(), [](const SystemSection& a, const SystemSection& b) {
        bool aKnown = systems::find(a.system) != nullptr;
        bool bKnown = systems::find(b.system) != nullptr;
        if (aKnown != bKnown) return aKnown;
        return foldCase(a.displayName) < foldCase(b.displayName);
    });
    return sections;
}

}  // namespace rm
