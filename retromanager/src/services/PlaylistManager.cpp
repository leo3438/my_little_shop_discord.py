#include "retromanager/services/PlaylistManager.hpp"

#include "retromanager/models/Systems.hpp"
#include "retromanager/parsers/PlaylistDocument.hpp"
#include "retromanager/platform/VirtualPath.hpp"
#include "retromanager/services/RetroArchPaths.hpp"

namespace rm {

PlaylistManager::PlaylistManager(IFileSystem& fs, SdLayout layout) : fs_(fs), layout_(std::move(layout)) {}

std::string PlaylistManager::playlistsDirectory() const {
    return retroarch::configuredDirectory(fs_, layout_, "playlist_directory", layout_.playlistsDir);
}

Result<std::string> PlaylistManager::playlistPathFor(const GameEntry& game) const {
    const SystemInfo* system = systems::find(game.system);
    if (system == nullptr || system->libretroName.empty()) {
        return makeError(ErrorCode::Unsupported, "no RetroArch playlist for system \"" + game.system + "\"");
    }
    return playlistsDirectory() + "/" + system->libretroName + ".lpl";
}

std::optional<Status> PlaylistManager::run(const GameEntry& game, const std::string& romPath, const CancellationToken&) {
    if (!retroarch::isInstalled(fs_, layout_)) {
        return Status(makeError(ErrorCode::NotFound, "RetroArch is not installed (" + layout_.retroarchDir + " is missing)"));
    }
    auto path = playlistPathFor(game);
    if (!path) return Status(path.error());

    auto existing = fs_.readFile(path.value());
    if (!existing && existing.error().code != ErrorCode::NotFound) return Status(existing.error());
    const bool fileExists = existing.ok();

    auto playlist = PlaylistDocument::parse(fileExists ? existing.value() : std::string());
    if (!playlist) return Status(makeError(ErrorCode::ParseError, path.value() + ": " + playlist.error().message));

    PlaylistItem item;
    item.path = romPath;
    item.label = vpath::stem(romPath);
    item.crc32 = PlaylistDocument::crcField(game.crc32);
    item.dbName = vpath::filename(path.value());
    if (!playlist.value().upsert(item) && !playlist.value().wasLegacy()) return success();  // already listed

    if (fileExists) {
        std::string backup = path.value() + ".rmbak";
        if (!fs_.exists(backup)) {
            if (Status saved = fs_.writeFile(backup, existing.value()); !saved) return saved;
        }
    }
    if (Status dir = fs_.createDirectories(vpath::parent(path.value())); !dir) return dir;
    return fs_.writeFile(path.value(), playlist.value().serialize());  // atomic replace
}

}  // namespace rm
