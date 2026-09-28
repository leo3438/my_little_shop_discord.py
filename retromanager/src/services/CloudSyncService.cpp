#include "retromanager/services/CloudSyncService.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <ctime>

#include "retromanager/core/Url.hpp"
#include "retromanager/platform/VirtualPath.hpp"
#include "retromanager/services/RetroArchPaths.hpp"

namespace rm {

namespace {

// "YYYYMMDD-HHMMSS" in UTC, without gmtime (not thread-safe, and missing
// its _r variant on some toolchains).
std::string utcStamp(std::int64_t seconds) {
    std::int64_t days = seconds >= 0 ? seconds / 86400 : (seconds - 86399) / 86400;
    std::int64_t rest = seconds - days * 86400;
    // civil_from_days (H. Hinnant)
    days += 719468;
    const std::int64_t era = (days >= 0 ? days : days - 146096) / 146097;
    const unsigned doe = static_cast<unsigned>(days - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const std::int64_t y = static_cast<std::int64_t>(yoe) + era * 400;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    const unsigned d = doy - (153 * mp + 2) / 5 + 1;
    const unsigned m = mp < 10 ? mp + 3 : mp - 9;
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%04lld%02u%02u-%02lld%02lld%02lld", static_cast<long long>(y + (m <= 2)), m, d,
                  static_cast<long long>(rest / 3600), static_cast<long long>(rest % 3600 / 60),
                  static_cast<long long>(rest % 60));
    return buffer;
}

}  // namespace

CloudSyncService::CloudSyncService(IFileSystem& fs, SdLayout layout, IRemoteSource& remote, std::string remoteBaseUrl,
                                   EventBus& bus, ISystem& system, std::unique_ptr<ITaskRunner> background,
                                   std::function<std::int64_t()> clock)
    : fs_(fs),
      layout_(std::move(layout)),
      remote_(remote),
      remoteBaseUrl_(std::move(remoteBaseUrl)),
      bus_(bus),
      awake_(system),
      clock_(clock ? std::move(clock) : [] { return static_cast<std::int64_t>(std::time(nullptr)); }),
      background_(std::move(background)) {
    if (!remoteBaseUrl_.empty() && remoteBaseUrl_.back() != '/') remoteBaseUrl_ += '/';
}

CloudSyncService::~CloudSyncService() {
    cancel();
    background_.reset();
}

bool CloudSyncService::isSaveFile(std::string_view name) {
    std::string base(name.substr(name.rfind('/') == std::string_view::npos ? 0 : name.rfind('/') + 1));
    if (base.empty() || isStagingName(base)) return false;
    std::string extension = vpath::extension("/" + base);
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    // .srm: most cores (SRAM). .sav: mGBA, melonDS... .dsv: DeSmuME.
    return extension == ".srm" || extension == ".sav" || extension == ".dsv";
}

std::string CloudSyncService::localSavesDirectory() const {
    return retroarch::configuredDirectory(fs_, layout_, "savefile_directory", layout_.savesDir);
}

std::string CloudSyncService::remoteUrlFor(const std::string& relative) const {
    return remoteBaseUrl_ + url::percentEncodePath(relative);
}

std::string CloudSyncService::conflictName(const std::string& localPath, const char* side) const {
    std::string base = localPath + ".conflict-" + side + "-" + utcStamp(clock_());
    std::string candidate = base;
    for (int n = 2; fs_.exists(candidate); ++n) candidate = base + "-" + std::to_string(n);
    return candidate;
}

Result<std::map<std::string, FileState>> CloudSyncService::scanLocal(const std::string& root) const {
    std::map<std::string, FileState> files;
    if (!fs_.isDirectory(root)) return files;  // nothing saved yet

    std::vector<std::pair<std::string, int>> pending = {{"", 0}};
    while (!pending.empty()) {
        auto [relative, depth] = pending.back();
        pending.pop_back();
        auto entries = fs_.listDirectory(relative.empty() ? root : root + "/" + relative);
        if (!entries) return entries.error();
        for (const DirEntry& entry : entries.value()) {
            std::string path = relative.empty() ? entry.name : relative + "/" + entry.name;
            if (entry.type == EntryType::Directory) {
                if (depth + 1 < kMaxDepth) pending.emplace_back(path, depth + 1);
            } else if (isSaveFile(entry.name)) {
                files[path] = FileState{entry.size, entry.modifiedAt};
            }
        }
    }
    return files;
}

Status CloudSyncService::scanRemoteDirectory(const std::string& relative, int depth,
                                             std::map<std::string, FileState>& out) const {
    auto entries = remote_.listDirectory(remoteUrlFor(relative.empty() ? "" : relative + "/"));
    if (!entries) {
        if (entries.error().code == ErrorCode::NotFound) return success();  // first sync: folder not created yet
        return entries.error();
    }
    for (const RemoteEntry& entry : entries.value()) {
        std::string path = relative.empty() ? entry.name : relative + "/" + entry.name;
        if (entry.isDirectory) {
            if (depth + 1 < kMaxDepth && !entry.name.empty() && entry.name.front() != '.') {
                if (Status s = scanRemoteDirectory(path, depth + 1, out); !s) return s;
            }
        } else if (isSaveFile(entry.name)) {
            out[path] = FileState{entry.size, entry.modifiedAt.value_or(0)};
        }
    }
    return success();
}

Result<std::map<std::string, FileState>> CloudSyncService::scanRemote() const {
    std::map<std::string, FileState> files;
    if (Status s = scanRemoteDirectory("", 0, files); !s) return s.error();
    return files;
}

SyncState CloudSyncService::loadState() const {
    auto document = fs_.readFile(syncStatePath());
    if (!document) return SyncState{};
    auto state = parseSyncState(document.value());
    // Unreadable, or recorded against another NAS folder: start over. The
    // planner then treats every file as a first meeting, which is safe.
    if (!state || state.value().remoteUrl != remoteBaseUrl_) return SyncState{};
    return state.value();
}

Status CloudSyncService::upload(const std::string& localPath, const std::string& relative, const CancellationToken& cancel) {
    auto info = fs_.stat(localPath);
    if (!info) return info.error();
    auto stream = fs_.openRead(localPath);
    if (!stream) return stream.error();
    IReadStream* input = stream.value().get();
    return remote_.uploadFile(
        remoteUrlFor(relative), [input](char* buffer, std::size_t capacity) { return input->read(buffer, capacity); },
        info.value().size, nullptr, cancel);
}

Status CloudSyncService::download(const std::string& relative, const std::string& localPath, const CancellationToken& cancel) {
    if (Status dir = fs_.createDirectories(vpath::parent(localPath)); !dir) return dir;
    auto stream = fs_.openWrite(localPath);  // hidden .tmp, renamed on close()
    if (!stream) return stream.error();
    IWriteStream* output = stream.value().get();
    Status transferred = remote_.downloadFile(
        remoteUrlFor(relative), [output](const char* data, std::size_t size) { return output->write(data, size); }, nullptr,
        cancel);
    if (!transferred) return transferred;  // the stream dies uncommitted: the .tmp is removed
    return output->close();
}

Result<SyncReport> CloudSyncService::syncNow(const CancellationToken& cancel,
                                             const std::function<void(const SyncProgressed&)>& progress) {
    if (!isConfigured()) return makeError(ErrorCode::NotConfigured, "no saves_url in config.json");
    std::unique_ptr<AwakeLock> keepAwake = awake_.acquire();

    const std::string root = localSavesDirectory();
    auto local = scanLocal(root);
    if (!local) return local.error();
    auto remote = scanRemote();
    if (!remote) return remote.error();

    SyncState previous = loadState();
    std::vector<PlannedAction> plan = planSync(local.value(), remote.value(), previous.files);

    SyncState next;
    next.remoteUrl = remoteBaseUrl_;
    std::vector<std::string> refreshRemote;  // uploaded: their NAS state is only known after a new listing
    SyncReport report;
    bool cancelled = false;

    auto localState = [&](const std::string& relative) -> std::optional<FileState> {
        auto info = fs_.stat(root + "/" + relative);
        if (!info) return std::nullopt;
        return FileState{info.value().size, info.value().modifiedAt};
    };

    for (std::size_t i = 0; i < plan.size(); ++i) {
        if (cancel.isCancelled()) {
            cancelled = true;
            break;
        }
        const PlannedAction& step = plan[i];
        const std::string localPath = root + "/" + step.path;
        Status status = success();

        switch (step.action) {
            case SyncAction::InSync:
                ++report.unchanged;
                next.files[step.path] = SyncRecord{local.value().at(step.path), remote.value().at(step.path)};
                break;
            case SyncAction::Upload:
                status = upload(localPath, step.path, cancel);
                if (status) {
                    ++report.uploaded;
                    refreshRemote.push_back(step.path);
                }
                break;
            case SyncAction::Download:
                status = download(step.path, localPath, cancel);
                if (status) {
                    ++report.downloaded;
                    if (auto l = localState(step.path)) next.files[step.path] = SyncRecord{*l, remote.value().at(step.path)};
                }
                break;
            case SyncAction::ConflictKeepLocal: {
                // Keep the NAS version on the card first, then publish ours.
                std::string copy = conflictName(localPath, "remote");
                status = download(step.path, copy, cancel);
                if (status) status = upload(localPath, step.path, cancel);
                if (status) {
                    ++report.conflicts;
                    report.conflictCopies.push_back(copy);
                    refreshRemote.push_back(step.path);
                }
                break;
            }
            case SyncAction::ConflictKeepRemote: {
                // Move our version aside, then bring the NAS one in; put ours
                // back if that fails.
                std::string copy = conflictName(localPath, "local");
                status = fs_.rename(localPath, copy);
                if (status) {
                    status = download(step.path, localPath, cancel);
                    if (!status) fs_.rename(copy, localPath).ok();
                }
                if (status) {
                    ++report.conflicts;
                    report.conflictCopies.push_back(copy);
                    if (auto l = localState(step.path)) next.files[step.path] = SyncRecord{*l, remote.value().at(step.path)};
                }
                break;
            }
        }

        if (!status) {
            if (status.error().code == ErrorCode::Cancelled) {
                cancelled = true;
                break;
            }
            report.failures.emplace_back(step.path, status.error());
            // Keep the old record (if any): the file is retried next time.
            if (auto old = previous.files.find(step.path); old != previous.files.end()) next.files[step.path] = old->second;
        }
        if (progress) progress(SyncProgressed{static_cast<int>(i + 1), static_cast<int>(plan.size()), step.path, step.action});
    }

    // Record what the NAS now holds for what we uploaded.
    if (!refreshRemote.empty()) {
        auto after = scanRemote();
        for (const std::string& path : refreshRemote) {
            auto l = localState(path);
            if (after && l && after.value().count(path) > 0) next.files[path] = SyncRecord{*l, after.value().at(path)};
            // Otherwise no record: the next sync compares both sides afresh,
            // which can only produce a transfer or a conflict copy, never a loss.
        }
    }

    // Files not reached before a cancellation keep their previous record.
    if (cancelled) {
        for (const auto& [path, record] : previous.files) next.files.emplace(path, record);
    }

    fs_.createDirectories(layout_.appDataDir).ok();
    if (Status saved = fs_.writeFile(syncStatePath(), serializeSyncState(next)); !saved) return saved.error();
    if (cancelled) return makeError(ErrorCode::Cancelled, "sync cancelled");
    return report;
}

bool CloudSyncService::start() {
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true)) return false;
    auto token = std::make_shared<CancellationToken>();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        current_ = token;
    }
    background_->runInBackground([this, token] {
        SyncReport report;
        Status result = success();
        auto outcome = syncNow(*token, [this](const SyncProgressed& p) { bus_.publish(p); });
        if (outcome) report = outcome.value();
        else result = outcome.error();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            current_.reset();
        }
        running_ = false;  // before the event: the UI may start another sync from its handler
        bus_.publish(SyncFinished{result, report});
    });
    return true;
}

void CloudSyncService::cancel() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (current_) current_->cancel();
}

}  // namespace rm
