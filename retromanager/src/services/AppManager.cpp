#include "retromanager/services/AppManager.hpp"

#include <memory>
#include <nlohmann/json.hpp>

#include "retromanager/core/FileName.hpp"
#include "retromanager/core/Url.hpp"
#include "retromanager/fs/FileInstall.hpp"
#include "retromanager/platform/VirtualPath.hpp"

namespace rm {

namespace {

using Json = nlohmann::json;

bool startsWith(const std::string& content, std::string_view prefix) {
    return content.size() >= prefix.size() && content.compare(0, prefix.size(), prefix) == 0;
}

}  // namespace

AppManager::AppManager(IFileSystem& fs, SdLayout layout, IRemoteSource& source)
    : fs_(fs), layout_(std::move(layout)), source_(source) {}

Result<std::string> AppManager::folderFor(const AppEntry& app) const {
    auto name = sanitizeFileName(app.folder);
    if (!name) return name.error();
    if (name.value() != app.folder) {
        return makeError(ErrorCode::InvalidArgument, "folder name \"" + app.folder + "\" is not FAT-safe");
    }
    return "/switch/" + name.value();
}

Result<std::string> AppManager::nroPathFor(const AppEntry& app) const {
    auto folder = folderFor(app);
    if (!folder) return folder.error();
    return folder.value() + "/" + app.folder + ".nro";
}

Result<std::string> AppManager::iconPathFor(const AppEntry& app) const {
    auto folder = folderFor(app);
    if (!folder) return folder.error();
    return folder.value() + "/icon.jpg";
}

Status AppManager::validateNroHeader(std::string_view header) {
    if (header.size() >= kNroHeaderBytes && header.substr(0x10, 4) == "NRO0") return success();
    return makeError(ErrorCode::IntegrityError, "not a Switch homebrew (no NRO0 header)");
}

Status AppManager::validateIcon(const std::string& content) {
    if (startsWith(content, "\xFF\xD8\xFF")) return success();
    if (startsWith(content, std::string_view("\x89PNG\r\n\x1a\n", 8)) || startsWith(content, "GIF8") ||
        startsWith(content, "BM") || startsWith(content, "RIFF")) {
        return makeError(ErrorCode::Unsupported, "icon is not a JPEG (hbmenu only reads JPEG icons)");
    }
    return makeError(ErrorCode::IntegrityError, "icon is not an image");
}

// --- installed versions ------------------------------------------------------

std::map<std::string, std::string> AppManager::loadRecords() {
    std::map<std::string, std::string> records;
    auto content = fs_.readFile(recordPath());
    if (!content) return records;
    Json doc = Json::parse(content.value(), nullptr, /*allow_exceptions=*/false);
    if (doc.is_discarded() || !doc.is_object() || !doc.contains("apps") || !doc["apps"].is_object()) return records;
    for (auto& [id, entry] : doc["apps"].items()) {
        if (entry.is_object() && entry.contains("version") && entry["version"].is_string()) {
            records[id] = entry["version"].get<std::string>();
        }
    }
    return records;
}

Status AppManager::record(const AppEntry& app, const std::string& path) {
    Json doc = Json::object();
    if (auto content = fs_.readFile(recordPath())) {
        Json existing = Json::parse(content.value(), nullptr, false);
        if (!existing.is_discarded() && existing.is_object() && existing.contains("apps") && existing["apps"].is_object()) {
            doc = std::move(existing);
        }
    }
    doc["version"] = 1;
    if (!doc.contains("apps")) doc["apps"] = Json::object();
    doc["apps"][app.id] = Json{{"version", app.version}, {"path", path}};
    if (Status dir = fs_.createDirectories(layout_.appDataDir); !dir) return dir;
    return fs_.writeFile(recordPath(), doc.dump(2) + "\n");
}

AppState AppManager::stateWith(const AppEntry& app, const std::map<std::string, std::string>& records) {
    auto path = nroPathFor(app);
    if (!path || !fs_.isFile(path.value())) return AppState::NotInstalled;
    auto it = records.find(app.id);
    if (it == records.end() || app.version.empty()) return AppState::Installed;  // nothing to compare
    return it->second == app.version ? AppState::Installed : AppState::UpdateAvailable;
}

AppState AppManager::state(const AppEntry& app) { return stateWith(app, loadRecords()); }

std::map<std::string, AppState> AppManager::states(const std::vector<AppEntry>& apps) {
    auto records = loadRecords();
    std::map<std::string, AppState> out;
    for (const AppEntry& app : apps) out[app.id] = stateWith(app, records);
    return out;
}

std::optional<std::string> AppManager::installedVersion(const AppEntry& app) {
    auto path = nroPathFor(app);
    if (!path || !fs_.isFile(path.value())) return std::nullopt;
    auto records = loadRecords();
    auto it = records.find(app.id);
    if (it == records.end()) return std::nullopt;
    return it->second;
}

// --- download --------------------------------------------------------------

std::uint64_t AppManager::remoteSize(const std::string& fileUrl) {
    std::size_t slash = fileUrl.rfind('/');
    if (slash == std::string::npos) return 0;
    std::string name = url::percentDecode(fileUrl.substr(slash + 1));
    auto listing = source_.listDirectory(fileUrl.substr(0, slash + 1));
    if (!listing) return 0;  // the transfer will tell
    for (const RemoteEntry& entry : listing.value()) {
        if (!entry.isDirectory && entry.name == name) return entry.size;
    }
    return 0;
}

Status AppManager::installIcon(const AppEntry& app, const CancellationToken& cancel) {
    auto path = iconPathFor(app);
    if (!path) return path.error();
    std::string image;
    Status downloaded = source_.downloadFile(
        app.iconUrl,
        [&image](const char* data, std::size_t size) -> Status {
            if (image.size() + size > kMaxIconBytes) {
                return makeError(ErrorCode::IoError, "icon larger than " + std::to_string(kMaxIconBytes) + " bytes");
            }
            image.append(data, size);
            return success();
        },
        nullptr, cancel);
    if (!downloaded) return downloaded;
    if (Status valid = validateIcon(image); !valid) return valid;
    return fs_.writeFile(path.value(), image);  // atomic
}

DownloadJob AppManager::job(const AppEntry& app) {
    DownloadJob job;
    job.itemId = app.id;
    job.title = app.title;
    job.url = app.nroUrl;
    job.sizeBytes = app.sizeBytes;
    auto path = nroPathFor(app);
    job.destination = path.valueOr("");

    // Shared by the callbacks: the size found on the server, and whether
    // the folder is ours to remove if the install fails.
    struct State {
        std::uint64_t size = 0;
        bool folderExisted = true;
    };
    auto state = std::make_shared<State>();
    state->size = app.sizeBytes;

    job.begin = [this, app, state]() -> Result<std::unique_ptr<FileInstall>> {
        auto destination = nroPathFor(app);
        if (!destination) return destination.error();
        // Unknown size: ask the server, so that the space check still happens.
        if (state->size == 0) state->size = remoteSize(app.nroUrl);
        state->folderExisted = fs_.exists(vpath::parent(destination.value()));
        auto install = beginFileInstall(fs_, destination.value(), state->size, app.crc32, app.title);
        if (install) install.value()->setHeaderCheck(kNroHeaderBytes, validateNroHeader);
        return install;
    };
    job.spaceReport = [this, app, state] { return checkSpace(fs_, nroPathFor(app).valueOr("/switch/x"), state->size); };
    job.onFailed = [this, app, state] {
        if (state->folderExisted) return;
        auto folder = folderFor(app);
        if (!folder) return;
        auto entries = fs_.listDirectory(folder.value());
        if (entries && entries.value().empty()) fs_.remove(folder.value()).ok();  // no empty folder in hbmenu
    };
    job.afterInstall = [this, app](const std::string&, const CancellationToken& cancel) {
        std::vector<StepOutcome> outcomes;
        if (!app.iconUrl.empty()) outcomes.push_back(StepOutcome{"icon", installIcon(app, cancel)});
        if (Status recorded = record(app, nroPathFor(app).valueOr("")); !recorded) {
            outcomes.push_back(StepOutcome{"record", recorded});
        }
        return outcomes;
    };
    return job;
}

}  // namespace rm
