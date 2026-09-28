#include "retromanager/services/CheatManager.hpp"

#include <algorithm>
#include <cctype>

#include "retromanager/models/Systems.hpp"
#include "retromanager/parsers/CfgDocument.hpp"
#include "retromanager/platform/VirtualPath.hpp"
#include "retromanager/services/RetroArchPaths.hpp"

namespace rm {

CheatManager::CheatManager(IFileSystem& fs, SdLayout layout, IRemoteSource& source)
    : fs_(fs), layout_(std::move(layout)), source_(source) {}

std::string CheatManager::cheatsDirectory() const {
    return retroarch::configuredDirectory(fs_, layout_, "cheat_database_path", layout_.cheatsDir);
}

Result<std::string> CheatManager::destinationFor(const GameEntry& game, const std::string& romPath) const {
    const SystemInfo* system = systems::find(game.system);
    if (system == nullptr || system->libretroName.empty()) {
        return makeError(ErrorCode::Unsupported, "no RetroArch cheat folder for system \"" + game.system + "\"");
    }
    std::string stem = vpath::stem(romPath);
    if (stem.empty()) return makeError(ErrorCode::InvalidArgument, "invalid ROM path " + romPath);
    return cheatsDirectory() + "/" + system->libretroName + "/" + stem + ".cht";
}

Status CheatManager::validate(const std::string& content) {
    auto count = CfgDocument::parse(content).get("cheats");
    bool isCount = count && !count->empty() &&
                   std::all_of(count->begin(), count->end(), [](unsigned char c) { return std::isdigit(c) != 0; });
    if (!isCount) return makeError(ErrorCode::IntegrityError, "not a RetroArch cheat file (no \"cheats = N\" line)");
    return success();
}

std::optional<Status> CheatManager::run(const GameEntry& game, const std::string& romPath,
                                        const CancellationToken& cancel) {
    if (game.cheatUrl.empty()) return std::nullopt;

    if (!retroarch::isInstalled(fs_, layout_)) {
        return Status(makeError(ErrorCode::NotFound, "RetroArch is not installed (" + layout_.retroarchDir + " is missing)"));
    }
    auto destination = destinationFor(game, romPath);
    if (!destination) return Status(destination.error());

    // Cheat files are a few KiB: buffer in memory, capped.
    std::string content;
    Status downloaded = source_.downloadFile(
        game.cheatUrl,
        [&content](const char* data, std::size_t size) -> Status {
            if (content.size() + size > kMaxCheatBytes) {
                return makeError(ErrorCode::IoError, "cheat file larger than " + std::to_string(kMaxCheatBytes) + " bytes");
            }
            content.append(data, size);
            return success();
        },
        nullptr, cancel);
    if (!downloaded) return downloaded;
    if (Status valid = validate(content); !valid) return valid;

    if (Status dir = fs_.createDirectories(vpath::parent(destination.value())); !dir) return dir;
    return fs_.writeFile(destination.value(), content);
}

}  // namespace rm
