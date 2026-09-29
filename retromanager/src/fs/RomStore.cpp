#include "retromanager/fs/RomStore.hpp"

#include <algorithm>

#include "retromanager/core/FileName.hpp"

namespace rm {

namespace {

bool isValidSystemId(const std::string& id) {
    return !id.empty() && std::all_of(id.begin(), id.end(), [](unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
    });
}

}  // namespace

RomStore::RomStore(IFileSystem& fs, SdLayout layout) : fs_(fs), layout_(std::move(layout)) {}

Result<std::string> RomStore::destinationFor(const GameEntry& game) const {
    if (!isValidSystemId(game.system)) return makeError(ErrorCode::InvalidArgument, "invalid system id \"" + game.system + "\"");
    auto name = sanitizeFileName(game.fileName);
    if (!name) return name.error();
    return layout_.romsDir + "/" + game.system + "/" + name.value();
}

bool RomStore::isInstalled(const GameEntry& game) {
    auto destination = destinationFor(game);
    return destination.ok() && fs_.isFile(destination.value());
}

SpaceReport RomStore::spaceReport(const GameEntry& game) {
    auto destination = destinationFor(game);
    return checkSpace(fs_, destination.ok() ? destination.value() : layout_.romsDir + "/x", game.sizeBytes);
}

Result<std::unique_ptr<FileInstall>> RomStore::beginInstall(const GameEntry& game) {
    auto destination = destinationFor(game);
    if (!destination) return destination.error();
    return beginFileInstall(fs_, destination.value(), game.sizeBytes, game.crc32, game.title);
}

}  // namespace rm
