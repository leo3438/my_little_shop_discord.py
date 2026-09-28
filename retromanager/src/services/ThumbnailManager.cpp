#include "retromanager/services/ThumbnailManager.hpp"

#include "retromanager/models/Systems.hpp"
#include "retromanager/platform/VirtualPath.hpp"
#include "retromanager/services/RetroArchPaths.hpp"

namespace rm {

namespace {

bool startsWith(const std::string& content, std::string_view prefix) {
    return content.size() >= prefix.size() && content.compare(0, prefix.size(), prefix) == 0;
}

}  // namespace

ThumbnailManager::ThumbnailManager(IFileSystem& fs, SdLayout layout, IRemoteSource& source)
    : fs_(fs), layout_(std::move(layout)), source_(source) {}

std::string ThumbnailManager::thumbnailsDirectory() const {
    return retroarch::configuredDirectory(fs_, layout_, "thumbnails_directory", layout_.thumbnailsDir);
}

Result<std::string> ThumbnailManager::destinationFor(const GameEntry& game, const std::string& romPath) const {
    const SystemInfo* system = systems::find(game.system);
    if (system == nullptr || system->libretroName.empty()) {
        return makeError(ErrorCode::Unsupported, "no RetroArch thumbnail folder for system \"" + game.system + "\"");
    }
    std::string label = vpath::stem(romPath);
    if (label.empty()) return makeError(ErrorCode::InvalidArgument, "invalid ROM path " + romPath);
    return thumbnailsDirectory() + "/" + system->libretroName + "/Named_Boxarts/" + retroarch::thumbnailName(label) + ".png";
}

Status ThumbnailManager::validate(const std::string& content) {
    if (startsWith(content, std::string_view("\x89PNG\r\n\x1a\n", 8))) return success();
    if (startsWith(content, "\xFF\xD8\xFF") || startsWith(content, "GIF8") || startsWith(content, "RIFF") ||
        startsWith(content, "BM")) {
        return makeError(ErrorCode::Unsupported, "box art is not a PNG (RetroArch only loads PNG thumbnails)");
    }
    return makeError(ErrorCode::IntegrityError, "box art is not an image");
}

std::optional<Status> ThumbnailManager::run(const GameEntry& game, const std::string& romPath,
                                            const CancellationToken& cancel) {
    if (game.boxartUrl.empty()) return std::nullopt;

    if (!retroarch::isInstalled(fs_, layout_)) {
        return Status(makeError(ErrorCode::NotFound, "RetroArch is not installed (" + layout_.retroarchDir + " is missing)"));
    }
    auto destination = destinationFor(game, romPath);
    if (!destination) return Status(destination.error());

    // Box art is a few hundred KiB: buffered in memory, capped, checked,
    // then written in one atomic replace.
    std::string image;
    Status downloaded = source_.downloadFile(
        game.boxartUrl,
        [&image](const char* data, std::size_t size) -> Status {
            if (image.size() + size > kMaxImageBytes) {
                return makeError(ErrorCode::IoError, "box art larger than " + std::to_string(kMaxImageBytes) + " bytes");
            }
            image.append(data, size);
            return success();
        },
        nullptr, cancel);
    if (!downloaded) return downloaded;
    if (Status valid = validate(image); !valid) return valid;

    if (Status dir = fs_.createDirectories(vpath::parent(destination.value())); !dir) return dir;
    return fs_.writeFile(destination.value(), image);
}

}  // namespace rm
