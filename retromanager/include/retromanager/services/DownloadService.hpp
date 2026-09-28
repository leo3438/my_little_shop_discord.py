#pragma once

#include <atomic>
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
#include "retromanager/fs/RomStore.hpp"
#include "retromanager/models/GameEntry.hpp"
#include "retromanager/network/IRemoteSource.hpp"
#include "retromanager/platform/ISystem.hpp"
#include "retromanager/services/PostInstallStep.hpp"

namespace rm {

using DownloadId = std::uint64_t;

// Events published on the EventBus (delivered on the main thread).
struct DownloadStarted {
    DownloadId id;
    GameEntry game;
    std::string destination;  // virtual path, empty if it could not be computed
};

struct DownloadProgressed {
    DownloadId id;
    std::uint64_t received;
    std::uint64_t total;  // 0 = unknown
    double bytesPerSecond;
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
    std::string gameId;
    std::vector<StepOutcome> steps;  // post-install outcomes, only after a successful install
};

// Downloads ROMs from the shop to the SD card, one at a time, off the UI
// thread. Progress and completion are reported through the EventBus.
//
// The console is kept awake from the start of a job to its end (transfer
// and post-install steps), whatever the outcome.
class DownloadService {
  public:
    // At most ~10 progress events per second per download.
    static constexpr std::chrono::milliseconds kProgressInterval{100};

    // `background` runs the transfers (a WorkerThread in the app: downloads
    // can take minutes and must not block the shared UI task loop). The
    // service owns it so that destruction can cancel and join safely.
    DownloadService(IRemoteSource& source, RomStore& store, EventBus& bus, ISystem& system,
                    std::unique_ptr<ITaskRunner> background);
    ~DownloadService();

    DownloadService(const DownloadService&) = delete;
    DownloadService& operator=(const DownloadService&) = delete;

    // Steps run in registration order after each successful install.
    // Register them before the first start(); they must outlive the service.
    void addPostInstallStep(IPostInstallStep& step) { steps_.push_back(&step); }

    // Queues a download; returns immediately. DownloadStarted is published
    // when the transfer actually begins.
    DownloadId start(const GameEntry& game);

    // The staging file is removed and DownloadFinished{Cancelled} published.
    // Unknown or finished ids are ignored.
    void cancel(DownloadId id);
    void cancelAll();

    std::size_t activeCount() const;

  private:
    void run(DownloadId id, const GameEntry& game, const std::shared_ptr<CancellationToken>& token);
    void finish(DownloadId id, const GameEntry& game, Status result, std::string destination, SpaceReport space = {},
                std::vector<StepOutcome> steps = {});

    IRemoteSource& source_;
    RomStore& store_;
    EventBus& bus_;
    AwakeLock::Holder awake_;
    std::vector<IPostInstallStep*> steps_;
    std::unique_ptr<ITaskRunner> background_;  // reset (joined) first thing in the destructor

    mutable std::mutex mutex_;
    std::map<DownloadId, std::shared_ptr<CancellationToken>> active_;
    std::atomic<DownloadId> nextId_{1};
};

}  // namespace rm
