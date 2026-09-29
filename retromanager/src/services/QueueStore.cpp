#include "retromanager/services/QueueStore.hpp"

#include <nlohmann/json.hpp>

#include "retromanager/parsers/EntryJson.hpp"
#include "retromanager/platform/VirtualPath.hpp"
#include "retromanager/services/AppManager.hpp"
#include "retromanager/services/DownloadQueueManager.hpp"

namespace rm {

namespace {

using Json = nlohmann::json;

}  // namespace

QueueStore::QueueStore(IFileSystem& fs, std::string path) : fs_(fs), path_(std::move(path)) {}

Status QueueStore::save(const std::vector<std::string>& payloads) {
    Json doc = {{"version", 1}, {"items", Json::array()}};
    for (const std::string& payload : payloads) {
        Json item = Json::parse(payload, nullptr, false);
        if (!item.is_discarded()) doc["items"].push_back(std::move(item));
    }
    if (Status dir = fs_.createDirectories(vpath::parent(path_)); !dir) return dir;
    return fs_.writeFile(path_, doc.dump(2) + "\n");
}

std::vector<std::string> QueueStore::load() const {
    std::vector<std::string> payloads;
    auto content = fs_.readFile(path_);
    if (!content) return payloads;
    Json doc = Json::parse(content.value(), nullptr, false);
    if (doc.is_discarded() || !doc.is_object() || !doc.contains("items") || !doc["items"].is_array()) return payloads;
    for (const Json& item : doc["items"]) payloads.push_back(item.dump());
    return payloads;
}

std::size_t restoreQueue(const std::vector<std::string>& payloads, DownloadQueueManager& queue, AppManager& apps) {
    std::size_t restored = 0;
    for (const std::string& payload : payloads) {
        Json item = Json::parse(payload, nullptr, false);
        if (item.is_discarded() || !item.is_object() || !item.contains("entry")) continue;
        const std::string kind = item.value("kind", "");
        const std::string entry = item["entry"].dump();
        if (kind == "rom") {
            if (auto game = gameFromJson(entry)) {
                queue.start(game.value());
                ++restored;
            }
        } else if (kind == "app") {
            if (auto app = appFromJson(entry)) {
                queue.start(apps.job(app.value()));
                ++restored;
            }
        }
    }
    return restored;
}

}  // namespace rm
