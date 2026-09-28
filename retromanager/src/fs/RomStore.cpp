#include "retromanager/fs/RomStore.hpp"

#include <algorithm>
#include <cctype>

#include "retromanager/core/Format.hpp"
#include "retromanager/platform/VirtualPath.hpp"

namespace rm {

namespace {

constexpr std::size_t kMaxFileNameBytes = 255;  // FAT32 / exFAT limit (UTF-16 units, bytes is stricter)

bool isValidSystemId(const std::string& id) {
    return !id.empty() && std::all_of(id.begin(), id.end(), [](unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
    });
}

// Characters FAT and exFAT refuse in file names.
bool isFatReserved(unsigned char c) {
    return c < 0x20 || c == 0x7F || c == '<' || c == '>' || c == ':' || c == '"' || c == '|' || c == '?' || c == '*';
}

}  // namespace

// --- RomInstall ----------------------------------------------------------

RomInstall::RomInstall(std::string destination, std::unique_ptr<BufferedWriteStream> stream, std::string expectedCrc32)
    : destination_(std::move(destination)), stream_(std::move(stream)), expectedCrc32_(std::move(expectedCrc32)) {}

Status RomInstall::write(const char* data, std::size_t size) {
    if (finished_) return makeError(ErrorCode::IoError, "install already finished");
    crc_.update(data, size);
    written_ += size;
    return stream_->write(data, size);
}

Status RomInstall::commit() {
    if (finished_) return makeError(ErrorCode::IoError, "install already finished");
    finished_ = true;
    // Checked before closing the stream: a corrupt download must never
    // replace a good file. The staging file dies with the install.
    if (!expectedCrc32_.empty() && crc_.hex() != expectedCrc32_) {
        return makeError(ErrorCode::IntegrityError, destination_ + ": CRC32 is " + crc_.hex() + ", the shop announced " +
                                                        expectedCrc32_);
    }
    return stream_->close();
}

// --- RomStore ------------------------------------------------------------

RomStore::RomStore(IFileSystem& fs, SdLayout layout) : fs_(fs), layout_(std::move(layout)) {}

Result<std::string> RomStore::destinationFor(const GameEntry& game) const {
    if (!isValidSystemId(game.system)) return makeError(ErrorCode::InvalidArgument, "invalid system id \"" + game.system + "\"");
    if (game.fileName.find_first_of("/\\") != std::string::npos) {
        return makeError(ErrorCode::InvalidArgument, "file name contains a path separator: \"" + game.fileName + "\"");
    }

    std::string name = game.fileName;
    for (char& c : name) {
        if (isFatReserved(static_cast<unsigned char>(c))) c = '_';
    }
    while (!name.empty() && (name.back() == '.' || name.back() == ' ')) name.pop_back();  // FAT drops them silently
    while (!name.empty() && name.front() == ' ') name.erase(name.begin());

    if (name.empty() || name.front() == '.') {
        // Leading dot: hidden file, and would collide with staging names.
        return makeError(ErrorCode::InvalidArgument, "unusable file name \"" + game.fileName + "\"");
    }
    if (name.size() > kMaxFileNameBytes) {
        return makeError(ErrorCode::InvalidArgument, "file name longer than 255 bytes: \"" + game.fileName + "\"");
    }
    return layout_.romsDir + "/" + game.system + "/" + name;
}

bool RomStore::isInstalled(const GameEntry& game) {
    auto destination = destinationFor(game);
    return destination.ok() && fs_.isFile(destination.value());
}

SpaceReport RomStore::spaceReport(const GameEntry& game) {
    SpaceReport report;
    if (game.sizeBytes == 0) return report;  // unknown size: nothing to compare
    report.requiredBytes = game.sizeBytes + kSpaceMargin;

    // Query the closest existing ancestor: /roms/<system> may not exist yet.
    auto destination = destinationFor(game);
    std::string probe = destination.ok() ? vpath::parent(destination.value()) : layout_.romsDir;
    while (probe != "/" && !fs_.exists(probe)) probe = vpath::parent(probe);

    auto available = fs_.availableSpace(probe);
    if (!available) return report;  // platform cannot tell: do not block
    report.availableBytes = available.value();
    report.sufficient = available.value() >= report.requiredBytes;
    return report;
}

Result<std::unique_ptr<RomInstall>> RomStore::beginInstall(const GameEntry& game) {
    auto destination = destinationFor(game);
    if (!destination) return destination.error();

    SpaceReport space = spaceReport(game);
    if (!space.sufficient) {
        return makeError(ErrorCode::InsufficientSpace,
                         game.title + " needs " + formatBytes(game.sizeBytes) + " (+" + formatBytes(kSpaceMargin) +
                             " margin), only " + formatBytes(space.availableBytes.value_or(0)) + " free on the SD card");
    }

    if (Status dir = fs_.createDirectories(vpath::parent(destination.value())); !dir) return dir.error();
    auto stream = fs_.openWrite(destination.value());
    if (!stream) return stream.error();

    auto buffered = std::make_unique<BufferedWriteStream>(std::move(stream.value()));
    return std::make_unique<RomInstall>(destination.value(), std::move(buffered), game.crc32);
}

}  // namespace rm
