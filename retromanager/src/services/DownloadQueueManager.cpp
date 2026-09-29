#include "retromanager/services/DownloadQueueManager.hpp"

#include <algorithm>

#include "retromanager/parsers/EntryJson.hpp"

namespace rm {

DownloadQueueManager::DownloadQueueManager(IRemoteSource& source, RomStore& store, EventBus& bus, ISystem& system,
                                 std::unique_ptr<ITaskRunner> background)
    : source_(source), store_(store), bus_(bus), awake_(system), background_(std::move(background)) {}

DownloadQueueManager::~DownloadQueueManager() {
    shuttingDown_ = true;  // quitting the app is not the user cancelling: keep what was downloaded
    cancelAll();
    background_.reset();  // joins a WorkerThread: the running transfer sees the cancellation and returns
}

DownloadId DownloadQueueManager::start(DownloadJob job) {
    DownloadId id = 0;
    bool post = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!job.itemId.empty()) {
            if (running_ && running_->item.itemId == job.itemId) return running_->item.id;
            for (const auto& entry : pending_) {
                if (entry->item.itemId == job.itemId) return entry->item.id;
            }
        }
        id = nextId_++;
        auto entry = std::make_unique<Entry>();
        entry->item.id = id;
        entry->item.kind = job.kind;
        entry->item.itemId = job.itemId;
        entry->item.title = job.title;
        entry->item.sizeBytes = job.sizeBytes;
        entry->token = std::make_shared<CancellationToken>();
        entry->job = std::move(job);
        pending_.push_back(std::move(entry));
        post = !draining_;
        draining_ = true;
    }
    publishChanged();
    if (post) background_->runInBackground([this] { drain(); });
    return id;
}

void DownloadQueueManager::drain() {
    std::unique_ptr<AwakeLock> keepAwake;  // held from the first item until the queue is empty
    while (true) {
        Entry* entry = nullptr;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (pending_.empty()) {
                draining_ = false;
                break;
            }
            running_ = std::move(pending_.front());
            pending_.pop_front();
            running_->item.state = QueueItemState::Running;
            entry = running_.get();  // only this thread replaces running_
        }
        if (!keepAwake) keepAwake = awake_.acquire();
        publishChanged();
        run(*entry);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            running_.reset();
        }
        publishChanged();
    }
}

bool DownloadQueueManager::cancel(DownloadId id) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (running_ && running_->item.id == id) {
            running_->token->cancel();  // DownloadFinished{Cancelled} follows
            return true;
        }
        auto it = std::find_if(pending_.begin(), pending_.end(), [id](const auto& e) { return e->item.id == id; });
        if (it == pending_.end()) return false;
        pending_.erase(it);
    }
    publishChanged();
    return true;
}

void DownloadQueueManager::cancelAll() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        pending_.clear();
        if (running_) running_->token->cancel();
    }
    publishChanged();
}

std::vector<QueueItem> DownloadQueueManager::snapshotLocked() const {
    std::vector<QueueItem> items;
    if (running_) items.push_back(running_->item);
    for (const auto& entry : pending_) items.push_back(entry->item);
    return items;
}

std::vector<QueueItem> DownloadQueueManager::snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return snapshotLocked();
}

std::vector<DownloadOutcome> DownloadQueueManager::history() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return {history_.begin(), history_.end()};
}

std::size_t DownloadQueueManager::activeCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return pending_.size() + (running_ ? 1 : 0);
}

bool DownloadQueueManager::isQueued(const std::string& itemId) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (running_ && running_->item.itemId == itemId) return true;
    return std::any_of(pending_.begin(), pending_.end(), [&](const auto& e) { return e->item.itemId == itemId; });
}

void DownloadQueueManager::publishChanged() {
    std::vector<QueueItem> items;
    std::vector<std::string> payloads;
    std::uint64_t seq = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        items = snapshotLocked();
        if (running_ && !running_->job.payload.empty()) payloads.push_back(running_->job.payload);
        for (const auto& entry : pending_) {
            if (!entry->job.payload.empty()) payloads.push_back(entry->job.payload);
        }
        seq = ++changeSeq_;
    }
    if (persist_ && !shuttingDown_) {
        std::lock_guard<std::mutex> lock(persistMutex_);
        if (seq > savedSeq_) {
            savedSeq_ = seq;
            persist_(payloads);
        }
    }
    bus_.publish(DownloadQueueChanged{std::move(items)});
}

DownloadId DownloadQueueManager::start(const GameEntry& game) {
    DownloadJob job;
    job.kind = DownloadKind::Rom;
    job.payload = queuePayload(game);
    job.system = game.system;
    job.itemId = game.id;
    job.title = game.title;
    job.url = game.romUrl;
    job.sizeBytes = game.sizeBytes;
    job.destination = store_.destinationFor(game).valueOr("");
    job.begin = [this, game] { return store_.beginInstall(game, /*resume=*/true); };
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

void DownloadQueueManager::finish(Entry& entry, Status result, SpaceReport space, std::vector<StepOutcome> steps) {
    const DownloadJob& job = entry.job;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        history_.push_front(
            DownloadOutcome{entry.item.id, job.kind, job.system, job.itemId, job.title, result, job.destination, space, steps});
        while (history_.size() > kHistorySize) history_.pop_back();
    }
    if (!result.ok() && job.onFailed) job.onFailed();
    bus_.publish(DownloadFinished{entry.item.id, std::move(result), job.destination, space, job.itemId, std::move(steps)});
}

void DownloadQueueManager::run(Entry& entry) {
    const DownloadJob& job = entry.job;
    const DownloadId id = entry.item.id;
    const std::shared_ptr<CancellationToken>& token = entry.token;
    bus_.publish(DownloadStarted{id, job.itemId, job.destination});

    if (token->isCancelled()) return finish(entry, makeError(ErrorCode::Cancelled, "download cancelled"));

    // Space check and staging happen before the server is contacted.
    auto install = job.begin();
    if (!install) {
        SpaceReport space = install.error().code == ErrorCode::InsufficientSpace && job.spaceReport ? job.spaceReport()
                                                                                                   : SpaceReport{};
        return finish(entry, install.error(), space);
    }

    using Clock = std::chrono::steady_clock;
    const Clock::time_point begin = Clock::now();
    Clock::time_point lastPublish{};  // epoch: the first chunk is always reported
    TransferProgress latest;
    std::uint64_t resumedFrom = install.value()->resumedFrom();

    auto publishProgress = [&](const TransferProgress& p) {
        double seconds = std::chrono::duration<double>(Clock::now() - begin).count();
        std::uint64_t fetched = p.received > resumedFrom ? p.received - resumedFrom : 0;
        double speed = seconds > 0 ? static_cast<double>(fetched) / seconds : 0.0;
        std::uint64_t total = p.total > 0 ? p.total : job.sizeBytes;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            entry.item.received = p.received;
            entry.item.total = total;
            entry.item.bytesPerSecond = speed;
            entry.item.resumedFrom = resumedFrom;
        }
        bus_.publish(DownloadProgressed{id, p.received, total, speed, resumedFrom});
    };
    auto sink = [&](const char* data, std::size_t size) { return install.value()->write(data, size); };
    auto onProgress = [&](const TransferProgress& p) {
        latest = p;
        Clock::time_point now = Clock::now();
        if (now - lastPublish >= kProgressInterval) {
            lastPublish = now;
            publishProgress(p);
        }
    };

    Status transferred = success();
    if (resumedFrom > 0 && job.sizeBytes > 0 && resumedFrom == job.sizeBytes) {
        // Everything is already there (interrupted right before the rename): just verify.
    } else if (resumedFrom > 0) {
        transferred = source_.downloadFileFrom(job.url, resumedFrom, sink, onProgress, *token);
        if (!transferred && transferred.error().code == ErrorCode::Unsupported) {
            // The server cannot resume: start over with an empty file.
            if (Status restarted = install.value()->restart(); !restarted) return finish(entry, restarted);
            resumedFrom = 0;
            transferred = source_.downloadFile(job.url, sink, onProgress, *token);
        }
    } else {
        transferred = source_.downloadFile(job.url, sink, onProgress, *token);
    }

    if (!transferred) {
        // A lost connection (or the app quitting) keeps the partial file for
        // a resume; a user cancellation or a bad file deletes it.
        const ErrorCode code = transferred.error().code;
        bool keep = code == ErrorCode::NetworkError || (code == ErrorCode::Cancelled && shuttingDown_);
        if (keep) install.value()->suspend().ok();
        install.value().reset();
        return finish(entry, transferred);
    }
    if (token->isCancelled()) {  // cancelled right after the last chunk
        install.value().reset();
        return finish(entry, makeError(ErrorCode::Cancelled, "download cancelled"));
    }

    latest.received = install.value()->bytesWritten();
    publishProgress(latest);  // always end on the final numbers
    Status committed = install.value()->commit();
    const std::string crc32 = install.value()->crc32();
    install.value().reset();
    if (!committed) return finish(entry, committed);

    // The file is safely installed from here on; follow-up work can only add to it.
    bus_.publish(DownloadConfiguring{id});
    std::vector<StepOutcome> outcomes = job.afterInstall ? job.afterInstall(crc32, *token) : std::vector<StepOutcome>{};
    finish(entry, committed, {}, std::move(outcomes));
}

}  // namespace rm
