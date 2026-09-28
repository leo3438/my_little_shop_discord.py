#include "retromanager/services/DownloadService.hpp"

namespace rm {

DownloadService::DownloadService(IRemoteSource& source, RomStore& store, EventBus& bus, ISystem& system,
                                 std::unique_ptr<ITaskRunner> background)
    : source_(source), store_(store), bus_(bus), awake_(system), background_(std::move(background)) {}

DownloadService::~DownloadService() {
    cancelAll();
    background_.reset();  // joins a WorkerThread: the running transfer sees the cancellation and returns
}

DownloadId DownloadService::start(const GameEntry& game) {
    DownloadId id = nextId_++;
    auto token = std::make_shared<CancellationToken>();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        active_[id] = token;
    }
    background_->runInBackground([this, id, game, token] { run(id, game, token); });
    return id;
}

void DownloadService::cancel(DownloadId id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = active_.find(id);
    if (it != active_.end()) it->second->cancel();
}

void DownloadService::cancelAll() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& entry : active_) entry.second->cancel();
}

std::size_t DownloadService::activeCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return active_.size();
}

void DownloadService::finish(DownloadId id, const GameEntry& game, Status result, std::string destination,
                             SpaceReport space, std::vector<StepOutcome> steps) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        active_.erase(id);
    }
    bus_.publish(DownloadFinished{id, std::move(result), std::move(destination), space, game.id, std::move(steps)});
}

void DownloadService::run(DownloadId id, const GameEntry& game, const std::shared_ptr<CancellationToken>& token) {
    std::unique_ptr<AwakeLock> keepAwake = awake_.acquire();  // released on every return path
    std::string destination = store_.destinationFor(game).valueOr("");
    bus_.publish(DownloadStarted{id, game, destination});

    if (token->isCancelled()) return finish(id, game, makeError(ErrorCode::Cancelled, "download cancelled"), destination);

    // Space check and staging happen before the NAS is contacted.
    auto install = store_.beginInstall(game);
    if (!install) {
        SpaceReport space = install.error().code == ErrorCode::InsufficientSpace ? store_.spaceReport(game) : SpaceReport{};
        return finish(id, game, install.error(), destination, space);
    }

    using Clock = std::chrono::steady_clock;
    const Clock::time_point begin = Clock::now();
    Clock::time_point lastPublish{};  // epoch: the first chunk is always reported
    TransferProgress latest;

    auto publishProgress = [&](const TransferProgress& p) {
        double seconds = std::chrono::duration<double>(Clock::now() - begin).count();
        double speed = seconds > 0 ? static_cast<double>(p.received) / seconds : 0.0;
        std::uint64_t total = p.total > 0 ? p.total : game.sizeBytes;
        bus_.publish(DownloadProgressed{id, p.received, total, speed});
    };

    Status transferred = source_.downloadFile(
        game.romUrl, [&](const char* data, std::size_t size) { return install.value()->write(data, size); },
        [&](const TransferProgress& p) {
            latest = p;
            Clock::time_point now = Clock::now();
            if (now - lastPublish >= kProgressInterval) {
                lastPublish = now;
                publishProgress(p);
            }
        },
        *token);

    if (!transferred) {
        install.value().reset();  // removes the staging file
        return finish(id, game, transferred, destination);
    }
    if (token->isCancelled()) {  // cancelled right after the last chunk
        install.value().reset();
        return finish(id, game, makeError(ErrorCode::Cancelled, "download cancelled"), destination);
    }

    latest.received = install.value()->bytesWritten();
    publishProgress(latest);  // always end on the final numbers
    Status committed = install.value()->commit();
    install.value().reset();
    if (!committed) return finish(id, game, committed, destination);

    // The ROM is safely installed from here on; steps can only add to it.
    bus_.publish(DownloadConfiguring{id});
    std::vector<StepOutcome> outcomes;
    for (IPostInstallStep* step : steps_) {
        if (std::optional<Status> outcome = step->run(game, destination, *token)) {
            outcomes.push_back(StepOutcome{step->id(), std::move(*outcome)});
        }
    }
    finish(id, game, committed, destination, {}, std::move(outcomes));
}

}  // namespace rm
