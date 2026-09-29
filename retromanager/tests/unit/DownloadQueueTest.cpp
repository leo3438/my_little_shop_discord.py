// The download queue: one transfer at a time, in order, with pending items
// that can be removed, a running one that can be cancelled, and the
// console kept awake until the queue is empty.

#include <gtest/gtest.h>

#include <chrono>
#include <deque>
#include <mutex>
#include <thread>

#include "MemoryFileSystem.hpp"
#include "retromanager/core/WorkerThread.hpp"
#include "retromanager/network/MockRemoteSource.hpp"
#include "retromanager/services/DownloadQueueManager.hpp"

using namespace rm;

namespace {

std::string urlFor(const std::string& name) { return "ftp://mock.local/shop/roms/nds/" + name + ".nds"; }

GameEntry game(const std::string& name, std::uint64_t size = 4096) {
    GameEntry entry;
    entry.id = "nds/" + name + ".nds";
    entry.title = name;
    entry.system = "nds";
    entry.fileName = name + ".nds";
    entry.sizeBytes = size;
    entry.romUrl = urlFor(name);
    return entry;
}

// Background work waits until the test says so: lets tests look at the
// queue while items are pending.
class ManualRunner : public ITaskRunner {
  public:
    void runInBackground(std::function<void()> task) override { tasks.push_back(std::move(task)); }
    void runOnMainThread(std::function<void()> task) override { task(); }
    void runAll() {
        while (!tasks.empty()) {
            auto task = std::move(tasks.front());
            tasks.pop_front();
            task();
        }
    }
    std::deque<std::function<void()>> tasks;
};

class FakeSystem : public ISystem {
  public:
    void setKeepAwake(bool keepAwake) override {
        awake = keepAwake;
        calls.push_back(keepAwake);
    }
    bool awake = false;
    std::vector<bool> calls;
};

struct Fixture {
    test::MemoryFileSystem fs;
    MockRemoteSource source{"{}", ""};
    RomStore store{fs, SdLayout{}};
    ImmediateTaskRunner mainThread;
    EventBus bus{mainThread};
    FakeSystem system;
    ManualRunner* worker = new ManualRunner();  // owned by the queue
    DownloadQueueManager queue{source, store, bus, system, std::unique_ptr<ITaskRunner>(worker)};

    std::vector<std::string> log;  // "start:A", "done:A"...
    std::vector<std::vector<QueueItem>> changes;
    EventBus::Subscription s1 = bus.subscribe<DownloadStarted>([this](const DownloadStarted& e) { log.push_back("start:" + e.itemId); });
    EventBus::Subscription s2 = bus.subscribe<DownloadFinished>([this](const DownloadFinished& e) {
        log.push_back(std::string(e.result.ok() ? "done:" : "fail:") + e.itemId);
    });
    EventBus::Subscription s3 = bus.subscribe<DownloadQueueChanged>([this](const DownloadQueueChanged& e) { changes.push_back(e.items); });

    Fixture() {
        for (const char* name : {"A", "B", "C"}) source.addSyntheticFile(urlFor(name), 4096);
    }
};

}  // namespace

TEST(DownloadQueue, RunsOneDownloadAtATimeInOrder) {
    Fixture f;
    DownloadId a = f.queue.start(game("A"));
    DownloadId b = f.queue.start(game("B"));
    DownloadId c = f.queue.start(game("C"));

    auto pending = f.queue.snapshot();
    ASSERT_EQ(pending.size(), 3u);
    EXPECT_EQ(pending[0].id, a);
    EXPECT_EQ(pending[1].id, b);
    EXPECT_EQ(pending[2].id, c);
    EXPECT_EQ(pending[0].title, "A");
    for (const QueueItem& item : pending) EXPECT_EQ(item.state, QueueItemState::Pending);
    EXPECT_EQ(pending[0].kind, DownloadKind::Rom);
    EXPECT_EQ(f.queue.activeCount(), 3u);

    f.worker->runAll();

    EXPECT_EQ(f.log, (std::vector<std::string>{"start:nds/A.nds", "done:nds/A.nds", "start:nds/B.nds", "done:nds/B.nds",
                                               "start:nds/C.nds", "done:nds/C.nds"}));
    EXPECT_TRUE(f.queue.snapshot().empty());
    EXPECT_EQ(f.queue.activeCount(), 0u);
    EXPECT_TRUE(f.fs.isFile("/roms/nds/C.nds"));
}

TEST(DownloadQueue, TheSameItemIsNeverQueuedTwice) {
    Fixture f;
    DownloadId first = f.queue.start(game("A"));
    EXPECT_EQ(f.queue.start(game("A")), first);
    EXPECT_EQ(f.queue.snapshot().size(), 1u);
    f.worker->runAll();
    EXPECT_EQ(f.source.downloadCount(), 1);
    EXPECT_NE(f.queue.start(game("A")), first);  // finished: can be downloaded again
}

TEST(DownloadQueue, APendingItemCanBeRemoved) {
    Fixture f;
    f.queue.start(game("A"));
    DownloadId b = f.queue.start(game("B"));
    f.queue.start(game("C"));

    EXPECT_TRUE(f.queue.cancel(b));
    ASSERT_EQ(f.queue.snapshot().size(), 2u);
    EXPECT_EQ(f.queue.snapshot()[1].title, "C");
    EXPECT_FALSE(f.queue.cancel(b));  // already gone

    f.worker->runAll();
    EXPECT_EQ(f.log, (std::vector<std::string>{"start:nds/A.nds", "done:nds/A.nds", "start:nds/C.nds", "done:nds/C.nds"}));
    EXPECT_FALSE(f.fs.exists("/roms/nds/B.nds"));
}

TEST(DownloadQueue, CancellingTheRunningItemMovesOnToTheNext) {
    Fixture f;
    f.source.addSyntheticFile(urlFor("A"), 400 * 1024);
    f.source.setThroughput(1024 * 1024);  // ~0.4 s
    DownloadId a = f.queue.start(game("A", 400 * 1024));
    f.queue.start(game("B"));
    EventBus::Subscription cancelA = f.bus.subscribe<DownloadProgressed>([&](const DownloadProgressed& e) {
        if (e.id == a && e.received > 50 * 1024) f.queue.cancel(a);
    });

    f.worker->runAll();

    EXPECT_EQ(f.log, (std::vector<std::string>{"start:nds/A.nds", "fail:nds/A.nds", "start:nds/B.nds", "done:nds/B.nds"}));
    EXPECT_FALSE(f.fs.exists(stagingPath("/roms/nds/A.nds")));  // a cancelled download leaves nothing
    EXPECT_TRUE(f.fs.isFile("/roms/nds/B.nds"));
}

TEST(DownloadQueue, TheConsoleStaysAwakeUntilTheQueueIsEmpty) {
    Fixture f;
    f.queue.start(game("A"));
    f.queue.start(game("B"));
    bool awakeBetweenItems = true;
    EventBus::Subscription probe = f.bus.subscribe<DownloadStarted>([&](const DownloadStarted&) {
        awakeBetweenItems = awakeBetweenItems && f.system.awake;
    });
    f.worker->runAll();
    EXPECT_TRUE(awakeBetweenItems);
    EXPECT_EQ(f.system.calls, (std::vector<bool>{true, false}));  // no sleep window between A and B
    EXPECT_FALSE(f.system.awake);
}

TEST(DownloadQueue, ItemsAddedWhileRunningJoinTheSameRun) {
    Fixture f;
    bool chained = false;
    EventBus::Subscription chain = f.bus.subscribe<DownloadFinished>([&](const DownloadFinished& e) {
        if (!chained && e.itemId == "nds/A.nds") {
            chained = true;
            f.queue.start(game("B"));
        }
    });
    f.queue.start(game("A"));
    f.worker->runAll();
    EXPECT_TRUE(f.fs.isFile("/roms/nds/B.nds"));
    EXPECT_EQ(f.system.calls, (std::vector<bool>{true, false}));
}

TEST(DownloadQueue, PublishesItsStateOnEveryChange) {
    Fixture f;
    f.queue.start(game("A"));
    ASSERT_FALSE(f.changes.empty());
    ASSERT_EQ(f.changes.back().size(), 1u);
    EXPECT_EQ(f.changes.back()[0].state, QueueItemState::Pending);

    std::vector<QueueItem> whileRunning;
    EventBus::Subscription probe = f.bus.subscribe<DownloadProgressed>([&](const DownloadProgressed&) { whileRunning = f.queue.snapshot(); });
    f.worker->runAll();

    ASSERT_EQ(whileRunning.size(), 1u);
    EXPECT_EQ(whileRunning[0].state, QueueItemState::Running);
    EXPECT_GT(whileRunning[0].received, 0u);
    EXPECT_TRUE(f.changes.back().empty());
    bool sawRunning = false;
    for (const auto& change : f.changes) {
        if (!change.empty() && change[0].state == QueueItemState::Running) sawRunning = true;
    }
    EXPECT_TRUE(sawRunning);
}

TEST(DownloadQueue, KeepsARecentHistory) {
    Fixture f;
    f.queue.start(game("A"));
    GameEntry missing = game("Missing");
    f.queue.start(missing);
    f.worker->runAll();

    auto history = f.queue.history();
    ASSERT_EQ(history.size(), 2u);
    EXPECT_EQ(history[0].title, "Missing");  // newest first
    EXPECT_EQ(history[0].result.error().code, ErrorCode::NotFound);
    EXPECT_EQ(history[1].title, "A");
    EXPECT_TRUE(history[1].result.ok());
    EXPECT_EQ(history[1].destination, "/roms/nds/A.nds");
    EXPECT_EQ(history[1].kind, DownloadKind::Rom);
    EXPECT_EQ(history[1].system, "nds");

    for (int i = 0; i < 40; ++i) f.queue.start(game("A"));
    for (int i = 0; i < 40; ++i) {
        f.queue.start(game("A"));
        f.worker->runAll();
    }
    EXPECT_EQ(f.queue.history().size(), DownloadQueueManager::kHistorySize);
}

TEST(DownloadQueue, CancelAllEmptiesTheQueue) {
    Fixture f;
    f.queue.start(game("A"));
    f.queue.start(game("B"));
    f.queue.cancelAll();
    EXPECT_TRUE(f.queue.snapshot().empty());
    f.worker->runAll();
    EXPECT_TRUE(f.log.empty());
}

TEST(DownloadQueue, OnARealWorkerTheUiThreadNeverWaits) {
    test::MemoryFileSystem fs;
    MockRemoteSource source("{}", "");
    source.addSyntheticFile(urlFor("A"), 8 * 1024 * 1024);
    source.addSyntheticFile(urlFor("B"), 8 * 1024 * 1024);
    source.setThroughput(16 * 1024 * 1024);  // ~0.5 s each
    RomStore store(fs, SdLayout{});
    std::mutex mutex;
    std::deque<std::function<void()>> mainQueue;
    class QueueRunner : public ITaskRunner {
      public:
        QueueRunner(std::mutex& m, std::deque<std::function<void()>>& q) : m_(m), q_(q) {}
        void runInBackground(std::function<void()> t) override { t(); }
        void runOnMainThread(std::function<void()> t) override {
            std::lock_guard<std::mutex> lock(m_);
            q_.push_back(std::move(t));
        }

      private:
        std::mutex& m_;
        std::deque<std::function<void()>>& q_;
    } mainThread(mutex, mainQueue);
    EventBus bus(mainThread);
    FakeSystem system;

    auto begin = std::chrono::steady_clock::now();
    {
        DownloadQueueManager queue(source, store, bus, system,
                                   std::make_unique<WorkerThread>([&](std::function<void()> t) { mainThread.runOnMainThread(t); }));
        queue.start(game("A", 8 * 1024 * 1024));
        queue.start(game("B", 8 * 1024 * 1024));
        EXPECT_LT(std::chrono::steady_clock::now() - begin, std::chrono::milliseconds(100));  // start() never blocks
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        auto items = queue.snapshot();
        ASSERT_EQ(items.size(), 2u);
        EXPECT_EQ(items[0].state, QueueItemState::Running);
        EXPECT_EQ(items[1].state, QueueItemState::Pending);
    }  // quitting: the running item stops, the pending one never starts
    EXPECT_LT(std::chrono::steady_clock::now() - begin, std::chrono::milliseconds(900));
    EXPECT_FALSE(fs.exists("/roms/nds/B.nds"));
    // Quitting is not cancelling: the partial file is kept for next time.
    EXPECT_TRUE(fs.exists(stagingPath("/roms/nds/A.nds")));
}
