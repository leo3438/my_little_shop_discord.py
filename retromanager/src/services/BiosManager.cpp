#include "retromanager/services/BiosManager.hpp"

#include <algorithm>
#include <cctype>

#include "retromanager/core/Md5.hpp"
#include "retromanager/services/RetroArchPaths.hpp"

namespace rm {

namespace {

std::string lower(std::string_view text) {
    std::string out(text);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

Result<std::string> md5OfFile(IFileSystem& fs, const std::string& path) {
    auto stream = fs.openRead(path);
    if (!stream) return stream.error();
    Md5 md5;
    char buffer[64 * 1024];
    while (true) {
        auto read = stream.value()->read(buffer, sizeof buffer);
        if (!read) return read.error();
        if (read.value() == 0) break;
        md5.update(buffer, read.value());
    }
    return md5.hex();
}

}  // namespace

BiosManager::BiosManager(IFileSystem& fs, SdLayout layout, IRemoteSource& source, EventBus& bus,
                         std::unique_ptr<ITaskRunner> background, std::vector<BiosFile> catalogue)
    : fs_(fs),
      layout_(std::move(layout)),
      source_(source),
      bus_(bus),
      catalogue_(std::move(catalogue)),
      background_(std::move(background)) {}

BiosManager::~BiosManager() {
    cancel();
    background_.reset();
}

std::string BiosManager::systemDirectory() const {
    return retroarch::configuredDirectory(fs_, layout_, "system_directory", layout_.systemDir);
}

std::string BiosManager::locate(const std::string& directory, const std::string& fileName) {
    const std::string wanted = lower(fileName);
    if (auto entries = fs_.listDirectory(directory)) {
        for (const DirEntry& entry : entries.value()) {
            if (entry.type == EntryType::File && lower(entry.name) == wanted) return directory + "/" + entry.name;
        }
    }
    return directory + "/" + fileName;
}

std::vector<BiosStatus> BiosManager::check(const std::vector<BiosEntry>& offers) {
    const std::string directory = systemDirectory();
    auto offerFor = [&](const std::string& fileName) -> std::optional<BiosEntry> {
        for (const BiosEntry& offer : offers) {
            if (lower(offer.fileName) == lower(fileName)) return offer;
        }
        return std::nullopt;
    };

    std::vector<BiosStatus> rows;
    auto addRow = [&](const std::string& system, const std::string& fileName, const std::string& description,
                      bool required, const std::string& referenceMd5) {
        BiosStatus row;
        row.system = system;
        row.fileName = fileName;
        row.description = description;
        row.required = required;
        row.path = locate(directory, fileName);
        row.offer = offerFor(fileName);
        if (fs_.isFile(row.path)) {
            std::string reference = !referenceMd5.empty() ? referenceMd5 : (row.offer ? row.offer->md5 : "");
            auto digest = md5OfFile(fs_, row.path);
            if (reference.empty()) row.state = BiosState::Unverified;
            else row.state = digest && digest.value() == reference ? BiosState::Ok : BiosState::Unrecognized;
        }
        rows.push_back(std::move(row));
    };

    for (const BiosFile& file : catalogue_) addRow(file.system, file.fileName, file.description, file.required, file.md5);
    for (const BiosEntry& offer : offers) {
        bool known = std::any_of(catalogue_.begin(), catalogue_.end(),
                                 [&](const BiosFile& file) { return lower(file.fileName) == lower(offer.fileName); });
        if (!known) addRow(offer.system, offer.fileName, "", false, offer.md5);
    }
    std::stable_sort(rows.begin(), rows.end(),
                     [](const BiosStatus& a, const BiosStatus& b) { return a.system < b.system; });
    return rows;
}

Status BiosManager::installNow(const BiosEntry& offer, const CancellationToken& cancel) {
    if (!bios::isSafeFileName(offer.fileName)) {
        return makeError(ErrorCode::PermissionDenied, "refusing to install \"" + offer.fileName + "\" outside the system folder");
    }
    if (!retroarch::isInstalled(fs_, layout_)) {
        return makeError(ErrorCode::NotFound, "RetroArch is not installed (" + layout_.retroarchDir + " is missing)");
    }
    if (cancel.isCancelled()) return makeError(ErrorCode::Cancelled, "cancelled");

    const std::string directory = systemDirectory();
    if (Status dir = fs_.createDirectories(directory); !dir) return dir;
    const std::string destination = locate(directory, offer.fileName);

    auto output = fs_.openWrite(destination);  // staged: visible only after close()
    if (!output) return output.error();
    Md5 md5;
    Status downloaded = source_.downloadFile(
        offer.url,
        [&](const char* data, std::size_t size) {
            md5.update(data, size);
            return output.value()->write(data, size);
        },
        nullptr, cancel);
    if (!downloaded) return downloaded;  // the stream is dropped: staging discarded
    if (cancel.isCancelled()) return makeError(ErrorCode::Cancelled, "cancelled");
    if (!offer.md5.empty() && md5.hex() != offer.md5) {
        return makeError(ErrorCode::IntegrityError,
                         offer.fileName + ": MD5 is " + md5.hex() + ", the shop announced " + offer.md5);
    }
    return output.value()->close();
}

bool BiosManager::startInstall(std::vector<BiosEntry> offers) {
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true)) return false;
    auto token = std::make_shared<CancellationToken>();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        current_ = token;
    }
    background_->runInBackground([this, token, offers = std::move(offers)] {
        BiosInstallFinished summary;
        for (const BiosEntry& offer : offers) {
            if (token->isCancelled()) break;
            Status result = installNow(offer, *token);
            (result ? summary.installed : summary.failed)++;
            bus_.publish(BiosInstalled{offer.fileName, result});
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            current_.reset();
        }
        running_ = false;  // before the event: the UI may start another install from its handler
        bus_.publish(summary);
    });
    return true;
}

void BiosManager::cancel() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (current_) current_->cancel();
}

}  // namespace rm
