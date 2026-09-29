#include "retromanager/services/DownloadService.hpp"

namespace rm {

DownloadService::DownloadService(IRemoteSource& source, RomStore& store, EventBus& bus, ISystem& system,
                                 std::unique_ptr<ITaskRunner> background)
    : source_(source), store_(store), bus_(bus), awake_(system), background_(std::move(background)) {}

DownloadService::~DownloadService() {
    cancelAll();
    background_.reset();  // joins a WorkerThread: the running transfer sees the cancellation and returns
}

DownloadId DownloadService::start(DownloadJob job) {
    DownloadId id = nextId_++;
    auto token = std::make_shared<CancellationToken>();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        active_[id] = token;
    }
    background_->runInBackground([this, id, job = std::move(job), token] { run(id, job, token); });
    return id;
}

DownloadId DownloadService::start(const GameEntry& game) {
    DownloadJob job;
    job.itemId = game.id;
    job.title = game.title;
    job.url = game.romUrl;
    job.sizeBytes = game.sizeBytes;
    job.destination = store_.destinationFor(game).valueOr("");
    job.begin = [this, game] { return store_.beginInstall(game); };
    job.spaceReport = [this, game] { return store_.spaceReport(game); };
    job.afterInstall = [this, game](const std::string& crc32, const CancellationToken& cancel) {
        // Steps see the CRC actually measured (playlists), even when the index had none.
        GameEntry installed = game;
        installed.crc32 = crc32;
        std::string destination = store_.destinationFor(game).valueOr("");
        std::vector<StepOutcome> outcomes;
        for (IPostInstallStep* step : steps_) {
            if (std::optional<Status> outcome = step->run(installed, destination, cancel)) {
                outcomes.push_back(StepOutcome{step->id(), std::move(*outcome)});
            }
        }
        return outcomes;
    };
    return start(std::move(job));
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

void DownloadService::finish(DownloadId id, const DownloadJob& job, Status result, SpaceReport space,
                             std::vector<StepOutcome> steps) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        active_.erase(id);
    }
    if (!result.ok() && job.onFailed) job.onFailed();
    bus_.publish(DownloadFinished{id, std::move(result), job.destination, space, job.itemId, std::move(steps)});
}

void DownloadService::run(DownloadId id, const DownloadJob& job, const std::shared_ptr<CancellationToken>& token) {
    std::unique_ptr<AwakeLock> keepAwake = awake_.acquire();  // released on every return path
    bus_.publish(DownloadStarted{id, job.itemId, job.destination});

    if (token->isCancelled()) return finish(id, job, makeError(ErrorCode::Cancelled, "download cancelled"));

    // Space check and staging happen before the server is contacted.
    auto install = job.begin();
    if (!install) {
        SpaceReport space = install.error().code == ErrorCode::InsufficientSpace && job.spaceReport ? job.spaceReport()
                                                                                                   : SpaceReport{};
        return finish(id, job, install.error(), space);
    }

    using Clock = std::chrono::steady_clock;
    const Clock::time_point begin = Clock::now();
    Clock::time_point lastPublish{};  // epoch: the first chunk is always reported
    TransferProgress latest;

    auto publishProgress = [&](const TransferProgress& p) {
        double seconds = std::chrono::duration<double>(Clock::now() - begin).count();
        double speed = seconds > 0 ? static_cast<double>(p.received) / seconds : 0.0;
        std::uint64_t total = p.total > 0 ? p.total : job.sizeBytes;
        bus_.publish(DownloadProgressed{id, p.received, total, speed});
    };

    Status transferred = source_.downloadFile(
        job.url, [&](const char* data, std::size_t size) { return install.value()->write(data, size); },
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
        return finish(id, job, transferred);
    }
    if (token->isCancelled()) {  // cancelled right after the last chunk
        install.value().reset();
        return finish(id, job, makeError(ErrorCode::Cancelled, "download cancelled"));
    }

    latest.received = install.value()->bytesWritten();
    publishProgress(latest);  // always end on the final numbers
    Status committed = install.value()->commit();
    const std::string crc32 = install.value()->crc32();
    install.value().reset();
    if (!committed) return finish(id, job, committed);

    // The file is safely installed from here on; follow-up work can only add to it.
    bus_.publish(DownloadConfiguring{id});
    std::vector<StepOutcome> outcomes = job.afterInstall ? job.afterInstall(crc32, *token) : std::vector<StepOutcome>{};
    finish(id, job, committed, {}, std::move(outcomes));
}

}  // namespace rm
