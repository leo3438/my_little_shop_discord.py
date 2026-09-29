#include "retromanager/services/ConfigManager.hpp"

#include "retromanager/parsers/ConfigParser.hpp"
#include "retromanager/platform/VirtualPath.hpp"

namespace rm {

ConfigManager::ConfigManager(IFileSystem& fs, std::string path) : fs_(fs), path_(std::move(path)) {}

Result<AppConfig> ConfigManager::loadOrCreate() {
    auto document = fs_.readFile(path_);
    if (!document) {
        if (document.error().code != ErrorCode::NotFound) return document.error();
        AppConfig defaults;
        if (Status saved = save(defaults); !saved) return saved.error();
        return defaults;
    }

    auto config = parseConfig(document.value());
    if (!config) return makeError(config.error().code, path_ + ": " + config.error().message);
    return config;
}

Status ConfigManager::save(const AppConfig& config) {
    if (Status dir = fs_.createDirectories(vpath::parent(path_)); !dir) return dir;
    const std::string text = serializeConfig(config);
    // The file may have been written by hand (comments, the user's layout):
    // the version being replaced is kept as config.json.bak.
    if (auto previous = fs_.readFile(path_); previous && previous.value() != text) {
        if (Status backup = fs_.writeFile(path_ + ".bak", previous.value()); !backup) return backup;
    }
    return fs_.writeFile(path_, text);
}

}  // namespace rm
