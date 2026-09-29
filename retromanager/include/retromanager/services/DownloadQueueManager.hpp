#pragma once

#include <atomic>
#include <deque>
#include <functional>
#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "retromanager/core/Cancellation.hpp"
#include "retromanager/core/EventBus.hpp"
#include "retromanager/core/ITaskRunner.hpp"
#include "retromanager/fs/FileInstall.hpp"
#include "retromanager/fs/RomStore.hpp"
#include "retromanager/models/GameEntry.hpp"
#include "retromanager/network/IRemoteSource.hpp"
#include "retromanager/platform/ISystem.hpp"
#include "retromanager/services/PostInstallStep.hpp"

namespace rm {

using DownloadId = std::uint64_t;

// One file to download and install: a ROM (start(GameEntry)), a homebrew
// (AppManager::job())... The service runs every job the same way: space
// check and staging before contacting the server, streamed transfer,
// commit, then the job's own follow-up work.
enum class DownloadKind { Rom, App };

struct DownloadJob {
    DownloadKind kind = DownloadKind::Rom;
    std::string system;         // ROMs: system id (for the summary)
    std::string itemId;         // reported back in DownloadFinished (GameEntry::id, AppEntry::id)
    std::string title;
    std::string url;
    std::uint64_t sizeBytes = 0;  // 0 = unknown
    std::string destination;      // virtual path shown to the user, empty if it could not be computed
    // Space check and staged output (on the worker thread), resuming a
    // partial file left by an interrupted attempt. Required.
    std::function<Result<std::unique_ptr<FileInstall>>()> begin;
    // Numbers for an InsufficientSpace failure. Optional.
    std::function<SpaceReport()> spaceReport;
    // After the commit, with the CRC-32 actually measured. Optional.
    std::function<std::vector<StepOutcome>(const std::string& crc32, const CancellationToken&)> afterInstall;
    // How to rebuild this job at the next launch (EntryJson queuePayload).
    // Empty = not persisted.
    std::string payload;
    // When the install fails or is cancelled (whatever the stage), after the
    // staging file is gone: lets the job tidy up what it created. Optional.
    std::function<void()> onFailed;
};

// Events published on the EventBus (delivered on the main thread).
struct DownloadStarted {
    DownloadId id;
    std::string itemId;
    std::string destination;  // virtual path, empty if it could not be computed
};

struct DownloadProgressed {
    DownloadId id;
    std::uint64_t received;  // whole file, resumed bytes included
    std::uint64_t total;     // 0 = unknown
    double bytesPerSecond;   // of this transfer
    std::uint64_t resumedFrom = 0;  // bytes kept from an interrupted attempt
};

// The ROM is on the card; post-install steps (emulator configuration,
// cheats...) are running.
struct DownloadConfiguring {
    DownloadId id;
};

struct DownloadFinished {
    DownloadId id;
    Status result;  // the ROM itself: ok, or Cancelled / InsufficientSpace / NetworkError / IntegrityError...
    std::string destination;
    SpaceReport space;  // meaningful when result is InsufficientSpace
    std::string itemId;
    std::vector<StepOutcome> steps;  // follow-up outcomes, only after a successful install
};

enum class QueueItemState { Pending, Running };

// One line of the downloads screen.
struct QueueItem {
    DownloadId id = 0;
    DownloadKind kind = DownloadKind::Rom;
    std::string itemId;
    std::string title;
    std::uint64_t sizeBytes = 0;
    QueueItemState state = QueueItemState::Pending;
    // Running item only:
    std::uint64_t received = 0;
    std::uint64_t total = 0;
    double bytesPerSecond = 0;
    std::uint64_t resumedFrom = 0;
};

// The queue changed (item added, removed, started, finished): its new
// content, running item first.
struct DownloadQueueChanged {
    std::vector<QueueItem> items;
};

// A finished download, kept for the downloads screen.
struct DownloadOutcome {
    DownloadId id = 0;
    DownloadKind kind = DownloadKind::Rom;
    std::string system;
    std::string itemId;
    std::string title;
    Status result;
    std::string destination;
    SpaceReport space;  // InsufficientSpace details
    std::vector<StepOutcome> steps;
};

// The download queue of the whole app: ROMs and homebrews are queued from
// any screen and downloaded one at a time, in order, on the worker thread.
// Pending items can be removed, the running one cancelled (its partial file
// is deleted); a lost connection keeps the partial file so that the next
// attempt resumes it. Progress and changes are reported through the
// EventBus; snapshot() and history() can be read from any thread.
//
// The console is kept awake from the first queued item until the queue is
// empty, whatever the outcomes.
class DownloadQueueManager {
  public:
    // At most ~10 progress events per second per download.
    static constexpr std::chrono::milliseconds kProgressInterval{100};
    static constexpr std::size_t kHistorySize = 30;

    // `background` runs the transfers (a WorkerThread in the app: downloads
    // can take minutes and must not block the shared UI task loop). The
    // queue owns it so that destruction can cancel and join safely.
    DownloadQueueManager(IRemoteSource& source, RomStore& store, EventBus& bus, ISystem& system,
                         std::unique_ptr<ITaskRunner> background);
    ~DownloadQueueManager();

    DownloadQueueManager(const DownloadQueueManager&) = delete;
    DownloadQueueManager& operator=(const DownloadQueueManager&) = delete;

    // Steps run in registration order after each successful install.
    // Register them before the first start(); they must outlive the queue.
    void addPostInstallStep(IPostInstallStep& step) { steps_.push_back(&step); }

    // Adds a download at the end of the queue; returns immediately. An item
    // already pending or running (same itemId) is not queued again: its id
    // is returned.
    DownloadId start(DownloadJob job);

    // A ROM: /roms/<system>/<file> through the RomStore, then the
    // post-install steps.
    DownloadId start(const GameEntry& game);

    // Pending: removed from the queue. Running: stopped, partial file
    // deleted, DownloadFinished{Cancelled} published. False when unknown or
    // already finished.
    bool cancel(DownloadId id);
    void cancelAll();

    // Called with the payloads of the items still to do (running first) after
    // every change of the queue, from the thread that changed it; never
    // while the app quits, so that queue.json keeps what was left. Set it
    // before queuing anything.
    void setPersistence(std::function<void(const std::vector<std::string>&)> save) { persist_ = std::move(save); }

    std::vector<QueueItem> snapshot() const;
    std::vector<DownloadOutcome> history() const;  // newest first
    std::size_t activeCount() const;               // pending + running
    bool isQueued(const std::string& itemId) const;

  private:
    struct Entry {
        DownloadJob job;
        std::shared_ptr<CancellationToken> token;
        QueueItem item;
    };

    void drain();
    void run(Entry& entry);
    void finish(Entry& entry, Status result, SpaceReport space = {}, std::vector<StepOutcome> steps = {});
    void publishChanged();  // and persists
    std::vector<QueueItem> snapshotLocked() const;

    IRemoteSource& source_;
    RomStore& store_;
    EventBus& bus_;
    AwakeLock::Holder awake_;
    std::atomic<bool> shuttingDown_{false};  // cancellations from the destructor keep partial files
    std::vector<IPostInstallStep*> steps_;
    std::unique_ptr<ITaskRunner> background_;  // reset (joined) first thing in the destructor

    mutable std::mutex mutex_;
    std::deque<std::unique_ptr<Entry>> pending_;
    std::unique_ptr<Entry> running_;
    bool draining_ = false;  // a drain() task is posted or running
    std::deque<DownloadOutcome> history_;
    std::atomic<DownloadId> nextId_{1};

    std::function<void(const std::vector<std::string>&)> persist_;
    std::mutex persistMutex_;       // one save at a time...
    std::uint64_t changeSeq_ = 0;   // (under mutex_) ...and never an older state over a newer one
    std::uint64_t savedSeq_ = 0;    // (under persistMutex_)
};

}  // namespace rm
