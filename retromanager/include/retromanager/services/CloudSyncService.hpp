#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "retromanager/core/Cancellation.hpp"
#include "retromanager/core/EventBus.hpp"
#include "retromanager/core/ITaskRunner.hpp"
#include "retromanager/network/IRemoteSource.hpp"
#include "retromanager/platform/IFileSystem.hpp"
#include "retromanager/platform/ISystem.hpp"
#include "retromanager/platform/SdLayout.hpp"
#include "retromanager/services/SyncPlanner.hpp"

namespace rm {

struct SyncReport {
    int uploaded = 0;
    int downloaded = 0;
    int unchanged = 0;
    int conflicts = 0;                         // resolved: winner in place, loser kept as a copy
    std::vector<std::string> conflictCopies;   // virtual paths of the kept losers
    std::vector<std::pair<std::string, Error>> failures;  // per file; the rest of the sync goes on
};

// Events (delivered on the main thread).
struct SyncProgressed {
    int done;
    int total;
    std::string path;  // file being handled, relative to the saves folder
    SyncAction action;
};

struct SyncFinished {
    Status result;  // ok, Cancelled, or why the sync could not run (network, config...)
    SyncReport report;
};

// Two-way synchronization of RetroArch save files (.srm, .sav, .dsv)
// between the SD card and a folder on the NAS.
//
// - Decisions: planSync(), from the current state of both sides and the
//   state recorded after the previous sync (SdLayout::appDataDir/sync-state.json).
// - Downloads land in a hidden .tmp next to the save and replace it only
//   once complete; uploads do the same on the NAS side.
// - A conflict never loses data: the losing version is kept next to the
//   winner as "<name>.conflict-{local|remote}-YYYYMMDD-HHMMSS".
// - One failing file is reported and skipped; the others still sync.
// - The console is kept awake for the whole run.
class CloudSyncService {
  public:
    static constexpr int kMaxDepth = 3;  // saves/<core>/<subfolder>/file

    // `remoteBaseUrl`: the NAS folder ("ftp://nas/Saves/"); empty = not configured.
    CloudSyncService(IFileSystem& fs, SdLayout layout, IRemoteSource& remote, std::string remoteBaseUrl, EventBus& bus,
                     ISystem& system, std::unique_ptr<ITaskRunner> background,
                     std::function<std::int64_t()> clock = nullptr);
    ~CloudSyncService();

    CloudSyncService(const CloudSyncService&) = delete;
    CloudSyncService& operator=(const CloudSyncService&) = delete;

    static bool isSaveFile(std::string_view name);

    bool isConfigured() const { return !remoteBaseUrl_.empty(); }
    const std::string& remoteBaseUrl() const { return remoteBaseUrl_; }
    std::string syncStatePath() const { return layout_.appDataDir + "/sync-state.json"; }

    // RetroArch's savefile_directory when retroarch.cfg sets an absolute
    // one, /retroarch/saves otherwise.
    std::string localSavesDirectory() const;

    // Runs a sync in the background; progress and the final report arrive
    // as events. Returns false (and does nothing) if one is already running.
    bool start();
    void cancel();
    bool running() const { return running_.load(); }

    // The whole sync on the calling thread.
    Result<SyncReport> syncNow(const CancellationToken& cancel,
                               const std::function<void(const SyncProgressed&)>& progress = nullptr);

  private:
    Result<std::map<std::string, FileState>> scanLocal(const std::string& root) const;
    Result<std::map<std::string, FileState>> scanRemote() const;
    Status scanRemoteDirectory(const std::string& relative, int depth, std::map<std::string, FileState>& out) const;
    std::string remoteUrlFor(const std::string& relative) const;
    std::string conflictName(const std::string& localPath, const char* side) const;

    Status upload(const std::string& localPath, const std::string& relative, const CancellationToken& cancel);
    Status download(const std::string& relative, const std::string& localPath, const CancellationToken& cancel);

    SyncState loadState() const;

    IFileSystem& fs_;
    SdLayout layout_;
    IRemoteSource& remote_;
    std::string remoteBaseUrl_;
    EventBus& bus_;
    AwakeLock::Holder awake_;
    std::function<std::int64_t()> clock_;
    std::unique_ptr<ITaskRunner> background_;  // reset (joined) first in the destructor

    std::mutex mutex_;
    std::shared_ptr<CancellationToken> current_;
    std::atomic<bool> running_{false};
};

}  // namespace rm
