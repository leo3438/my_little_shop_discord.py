#include <gtest/gtest.h>

#include <algorithm>
#include <deque>
#include <vector>

#include "MemoryFileSystem.hpp"
#include "retromanager/core/Crc32.hpp"
#include "retromanager/core/WorkerThread.hpp"
#include "retromanager/network/MockRemoteSource.hpp"
#include "retromanager/services/DownloadQueueManager.hpp"

using namespace rm;

namespace {

const std::string kUrl = "ftp://mock.local/shop/roms/nds/Game.nds";

GameEntry game(std::uint64_t size, std::string url = kUrl) {
    GameEntry entry;
    entry.id = "nds/Game.nds";
    entry.title = "Game";
    entry.system = "nds";
    entry.fileName = "Game.nds";
    entry.sizeBytes = size;
    entry.romUrl = std::move(url);
    return entry;
}

// Records keep-awake requests.
class FakeSystem : public ISystem {
  public:
    void setKeepAwake(bool keepAwake) override {
        awake = keepAwake;
        calls.push_back(keepAwake);
    }
    bool awake = false;
    std::vector<bool> calls;
};

// Scripted post-install step.
class FakeStep : public IPostInstallStep {
  public:
    FakeStep(std::string id, std::optional<Status> outcome) : id_(std::move(id)), outcome_(std::move(outcome)) {}
    std::string id() const override { return id_; }
    std::optional<Status> run(const GameEntry& game, const std::string& romPath, const CancellationToken&) override {
        ranFor.push_back(game.id);
        paths.push_back(romPath);
        crcs.push_back(game.crc32);
        return outcome_;
    }
    std::vector<std::string> ranFor;
    std::vector<std::string> paths;
    std::vector<std::string> crcs;

  private:
    std::string id_;
    std::optional<Status> outcome_;
};

// Everything synchronous: start() runs the whole download inline and events
// are delivered immediately, so tests read like a script.
struct Fixture {
    test::MemoryFileSystem fs;
    MockRemoteSource source{"{}", ""};
    RomStore store{fs, SdLayout{}};
    ImmediateTaskRunner mainThread;
    EventBus bus{mainThread};
    FakeSystem system;
    DownloadQueueManager downloads{source, store, bus, system, std::make_unique<ImmediateTaskRunner>()};

    std::vector<DownloadStarted> started;
    std::vector<DownloadProgressed> progress;
    std::vector<DownloadFinished> finished;
    EventBus::Subscription s1 = bus.subscribe<DownloadStarted>([this](const DownloadStarted& e) { started.push_back(e); });
    EventBus::Subscription s2 =
        bus.subscribe<DownloadProgressed>([this](const DownloadProgressed& e) { progress.push_back(e); });
    EventBus::Subscription s3 = bus.subscribe<DownloadFinished>([this](const DownloadFinished& e) { finished.push_back(e); });
    std::vector<std::string> order;  // event sequence
    EventBus::Subscription s4 =
        bus.subscribe<DownloadProgressed>([this](const DownloadProgressed&) { order.push_back("progress"); });
    EventBus::Subscription s5 =
        bus.subscribe<DownloadConfiguring>([this](const DownloadConfiguring&) { order.push_back("configuring"); });
    EventBus::Subscription s6 = bus.subscribe<DownloadFinished>([this](const DownloadFinished&) { order.push_back("finished"); });
};

}  // namespace

// --- MockRemoteSource streaming ------------------------------------------

TEST(MockRemoteSourceDownload, StreamsInChunksWithProgress) {
    MockRemoteSource source("{}", "");
    source.addFile(kUrl, std::string(100000, 'r'));
    source.setChunkSize(16384);

    std::string received;
    std::size_t chunks = 0;
    std::vector<TransferProgress> ticks;
    CancellationToken cancel;
    Status status = source.downloadFile(
        kUrl,
        [&](const char* data, std::size_t size) {
            EXPECT_LE(size, 16384u);
            ++chunks;
            received.append(data, size);
            return success();
        },
        [&](const TransferProgress& p) { ticks.push_back(p); }, cancel);

    ASSERT_TRUE(status.ok()) << status.error().describe();
    EXPECT_EQ(received, std::string(100000, 'r'));
    EXPECT_EQ(chunks, 7u);
    ASSERT_FALSE(ticks.empty());
    EXPECT_EQ(ticks.back().received, 100000u);
    EXPECT_EQ(ticks.back().total, 100000u);
}

TEST(MockRemoteSourceDownload, SyntheticFilesAreGeneratedOnTheFly) {
    MockRemoteSource source("{}", "");
    source.addSyntheticFile(kUrl, 1000);
    std::string received;
    CancellationToken cancel;
    ASSERT_TRUE(source
                    .downloadFile(
                        kUrl,
                        [&](const char* d, std::size_t n) {
                            received.append(d, n);
                            return success();
                        },
                        nullptr, cancel)
                    .ok());
    ASSERT_EQ(received.size(), 1000u);
    for (std::size_t i = 0; i < received.size(); ++i) ASSERT_EQ(received[i], MockRemoteSource::syntheticByte(i));
}

TEST(MockRemoteSourceDownload, DemoShopServesEveryEntry) {
    MockRemoteSource demo;
    EXPECT_EQ(demo.indexUrl(), MockRemoteSource::kDemoIndexUrl);
    std::uint64_t bytes = 0;
    CancellationToken cancel;
    // Tetris: the smallest demo entry, 32 KiB.
    ASSERT_TRUE(demo.downloadFile(
                        "ftp://mock.local/shop/roms/gb/Tetris%20(World).gb",
                        [&](const char*, std::size_t n) {
                            bytes += n;
                            return success();
                        },
                        nullptr, cancel)
                    .ok());
    EXPECT_EQ(bytes, 32768u);
}

TEST(MockRemoteSourceDownload, ErrorsAndCancellation) {
    MockRemoteSource source("{}", "");
    source.addFile(kUrl, std::string(50000, 'x'));
    CancellationToken cancel;
    auto ignore = [](const char*, std::size_t) { return success(); };

    EXPECT_EQ(source.downloadFile("ftp://mock.local/nope", ignore, nullptr, cancel).error().code, ErrorCode::NotFound);

    Status sinkError = source.downloadFile(
        kUrl, [](const char*, std::size_t) { return Status(makeError(ErrorCode::IoError, "SD full")); }, nullptr, cancel);
    EXPECT_EQ(sinkError.error().code, ErrorCode::IoError);  // the sink's own error comes back

    CancellationToken cancelled;
    std::size_t delivered = 0;
    Status stopped = source.downloadFile(
        kUrl,
        [&](const char*, std::size_t n) {
            delivered += n;
            cancelled.cancel();  // user pressed B after the first chunk
            return success();
        },
        nullptr, cancelled);
    EXPECT_EQ(stopped.error().code, ErrorCode::Cancelled);
    EXPECT_LT(delivered, 50000u);

    source.setDownloadFailure(20000, makeError(ErrorCode::NetworkError, "connection reset"));
    EXPECT_EQ(source.downloadFile(kUrl, ignore, nullptr, cancel).error().code, ErrorCode::NetworkError);
}

// --- DownloadQueueManager -----------------------------------------------------

TEST(DownloadQueueManager, DownloadsToTheSdCardAndReportsEachStep) {
    Fixture f;
    f.source.addSyntheticFile(kUrl, 3 * 1024 * 1024 + 123);

    DownloadId id = f.downloads.start(game(3 * 1024 * 1024 + 123));

    ASSERT_EQ(f.started.size(), 1u);
    EXPECT_EQ(f.started[0].id, id);
    EXPECT_EQ(f.started[0].destination, "/roms/nds/Game.nds");
    ASSERT_FALSE(f.progress.empty());
    EXPECT_EQ(f.progress.back().received, 3u * 1024 * 1024 + 123);
    EXPECT_EQ(f.progress.back().total, 3u * 1024 * 1024 + 123);
    ASSERT_EQ(f.finished.size(), 1u);
    EXPECT_TRUE(f.finished[0].result.ok()) << f.finished[0].result.error().describe();
    EXPECT_EQ(f.finished[0].destination, "/roms/nds/Game.nds");

    auto content = f.fs.readFile("/roms/nds/Game.nds");
    ASSERT_TRUE(content.ok());
    ASSERT_EQ(content.value().size(), 3u * 1024 * 1024 + 123);
    EXPECT_EQ(content.value()[1234567], MockRemoteSource::syntheticByte(1234567));
    EXPECT_EQ(f.downloads.activeCount(), 0u);
}

TEST(DownloadQueueManager, ProgressEventsAreThrottled) {
    Fixture f;
    f.source.addSyntheticFile(kUrl, 8 * 1024 * 1024);
    f.source.setChunkSize(4096);  // 2048 chunks, all delivered within a few ms
    f.downloads.start(game(8 * 1024 * 1024));

    ASSERT_EQ(f.finished.size(), 1u);
    EXPECT_TRUE(f.finished[0].result.ok());
    EXPECT_LT(f.progress.size(), 50u);  // not one event per chunk
    EXPECT_EQ(f.progress.back().received, 8u * 1024 * 1024);  // the final state is always published
}

TEST(DownloadQueueManager, InsufficientSpaceFailsBeforeAnyNetworkAccess) {
    Fixture f;
    f.fs.setCapacity(10 * 1024 * 1024);
    f.source.addSyntheticFile(kUrl, 128 * 1024 * 1024);

    f.downloads.start(game(128 * 1024 * 1024));

    ASSERT_EQ(f.finished.size(), 1u);
    EXPECT_EQ(f.finished[0].result.error().code, ErrorCode::InsufficientSpace);
    EXPECT_FALSE(f.finished[0].space.sufficient);
    EXPECT_EQ(f.finished[0].space.availableBytes, 10u * 1024 * 1024);
    EXPECT_EQ(f.source.downloadCount(), 0);  // the NAS was never contacted
    EXPECT_FALSE(f.fs.exists("/roms/nds"));
}

TEST(DownloadQueueManager, CancelMidTransferCleansUp) {
    Fixture f;
    f.source.addSyntheticFile(kUrl, 4 * 1024 * 1024);
    f.source.setChunkSize(64 * 1024);
    // Cancel as soon as the first progress event arrives (the user presses B).
    EventBus::Subscription cancelOnProgress =
        f.bus.subscribe<DownloadProgressed>([&](const DownloadProgressed& e) { f.downloads.cancel(e.id); });

    f.downloads.start(game(4 * 1024 * 1024));

    ASSERT_EQ(f.finished.size(), 1u);
    EXPECT_EQ(f.finished[0].result.error().code, ErrorCode::Cancelled);
    EXPECT_FALSE(f.fs.exists("/roms/nds/Game.nds"));
    EXPECT_TRUE(f.fs.listDirectory("/roms/nds").value().empty());  // no staging file left
    EXPECT_EQ(f.fs.usedBytes(), 0u);
}

TEST(DownloadQueueManager, CancelledPreviousVersionSurvives) {
    Fixture f;
    ASSERT_TRUE(f.fs.createDirectories("/roms/nds").ok());
    ASSERT_TRUE(f.fs.writeFile("/roms/nds/Game.nds", "old version").ok());
    f.source.addSyntheticFile(kUrl, 1024 * 1024);
    f.source.setDownloadFailure(300000, makeError(ErrorCode::NetworkError, "connection reset by NAS"));

    f.downloads.start(game(1024 * 1024));

    ASSERT_EQ(f.finished.size(), 1u);
    EXPECT_EQ(f.finished[0].result.error().code, ErrorCode::NetworkError);
    EXPECT_EQ(f.fs.readFile("/roms/nds/Game.nds").value(), "old version");
}

TEST(DownloadQueueManager, IntegrityFailureDiscardsTheFile) {
    Fixture f;
    f.source.addFile(kUrl, "123456789");
    GameEntry entry = game(9);
    entry.crc32 = "00000000";

    f.downloads.start(entry);

    ASSERT_EQ(f.finished.size(), 1u);
    EXPECT_EQ(f.finished[0].result.error().code, ErrorCode::IntegrityError);
    EXPECT_FALSE(f.fs.exists("/roms/nds/Game.nds"));
}

TEST(DownloadQueueManager, UnsafeFileNameIsRejected) {
    Fixture f;
    GameEntry entry = game(10);
    entry.fileName = "..";
    f.downloads.start(entry);
    ASSERT_EQ(f.finished.size(), 1u);
    EXPECT_EQ(f.finished[0].result.error().code, ErrorCode::InvalidArgument);
    EXPECT_EQ(f.source.downloadCount(), 0);
}

TEST(DownloadQueueManager, RunsOnItsWorkerAndJoinsOnDestruction) {
    test::MemoryFileSystem fs;
    MockRemoteSource source("{}", "");
    source.addSyntheticFile(kUrl, 64 * 1024 * 1024);
    source.setThroughput(32 * 1024 * 1024);  // ~2 s if left alone
    RomStore store(fs, SdLayout{});

    std::mutex mainMutex;
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
    } mainThread(mainMutex, mainQueue);
    EventBus bus(mainThread);

    auto begin = std::chrono::steady_clock::now();
    {
        NullSystem system;
        DownloadQueueManager downloads(source, store, bus, system,
                                  std::make_unique<WorkerThread>([&](std::function<void()> t) { mainThread.runOnMainThread(t); }));
        downloads.start(game(64 * 1024 * 1024));
        EXPECT_EQ(downloads.activeCount(), 1u);  // start() returned while the transfer runs
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }  // destructor cancels and joins
    auto elapsed = std::chrono::steady_clock::now() - begin;

    EXPECT_LT(elapsed, std::chrono::milliseconds(1500));
    EXPECT_FALSE(fs.exists("/roms/nds/Game.nds"));
}

// --- keep awake ------------------------------------------------------------

TEST(DownloadQueueManager, KeepsTheConsoleAwakeDuringTheTransferOnly) {
    Fixture f;
    f.source.addSyntheticFile(kUrl, 1024 * 1024);
    bool awakeWhileTransferring = false;
    EventBus::Subscription probe =
        f.bus.subscribe<DownloadProgressed>([&](const DownloadProgressed&) { awakeWhileTransferring = f.system.awake; });

    f.downloads.start(game(1024 * 1024));

    EXPECT_TRUE(awakeWhileTransferring);
    EXPECT_EQ(f.system.calls, (std::vector<bool>{true, false}));
    EXPECT_FALSE(f.system.awake);
}

TEST(DownloadQueueManager, ReleasesTheAwakeLockOnEveryFailurePath) {
    {  // cancelled
        Fixture f;
        f.source.addSyntheticFile(kUrl, 4 * 1024 * 1024);
        EventBus::Subscription cancel =
            f.bus.subscribe<DownloadProgressed>([&](const DownloadProgressed& e) { f.downloads.cancel(e.id); });
        f.downloads.start(game(4 * 1024 * 1024));
        EXPECT_EQ(f.finished.at(0).result.error().code, ErrorCode::Cancelled);
        EXPECT_FALSE(f.system.awake);
    }
    {  // network error
        Fixture f;
        f.source.addSyntheticFile(kUrl, 1024 * 1024);
        f.source.setDownloadFailure(1000, makeError(ErrorCode::NetworkError, "reset"));
        f.downloads.start(game(1024 * 1024));
        EXPECT_FALSE(f.system.awake);
        EXPECT_EQ(f.system.calls, (std::vector<bool>{true, false}));
    }
    {  // refused before any transfer
        Fixture f;
        f.fs.setCapacity(1024);
        f.downloads.start(game(1024 * 1024));
        EXPECT_FALSE(f.system.awake);
    }
}

TEST(AwakeLock, IsReferenceCounted) {
    FakeSystem system;
    AwakeLock::Holder holder(system);
    auto first = holder.acquire();
    auto second = holder.acquire();
    EXPECT_EQ(system.calls, (std::vector<bool>{true}));  // one request for two users
    first.reset();
    EXPECT_TRUE(system.awake);
    second.reset();
    EXPECT_EQ(system.calls, (std::vector<bool>{true, false}));
    EXPECT_EQ(holder.activeLocks(), 0);
}

// --- post-install steps ----------------------------------------------------

TEST(DownloadQueueManager, RunsPostInstallStepsAfterTheRomIsCommitted) {
    Fixture f;
    f.source.addSyntheticFile(kUrl, 1000);
    FakeStep retroarch("retroarch", success());
    FakeStep cheats("cheats", std::nullopt);  // not applicable to this game
    f.downloads.addPostInstallStep(retroarch);
    f.downloads.addPostInstallStep(cheats);
    bool romPresentDuringSteps = false;
    EventBus::Subscription probe = f.bus.subscribe<DownloadConfiguring>(
        [&](const DownloadConfiguring&) { romPresentDuringSteps = f.fs.isFile("/roms/nds/Game.nds"); });

    f.downloads.start(game(1000));

    EXPECT_TRUE(romPresentDuringSteps);
    EXPECT_EQ(retroarch.paths, (std::vector<std::string>{"/roms/nds/Game.nds"}));
    EXPECT_EQ(cheats.ranFor.size(), 1u);
    ASSERT_EQ(f.finished.size(), 1u);
    EXPECT_TRUE(f.finished[0].result.ok());
    EXPECT_EQ(f.finished[0].itemId, "nds/Game.nds");
    ASSERT_EQ(f.finished[0].steps.size(), 1u);  // the nullopt step is not reported
    EXPECT_EQ(f.finished[0].steps[0].id, "retroarch");
    EXPECT_TRUE(f.finished[0].steps[0].result.ok());

    ASSERT_GE(f.order.size(), 3u);
    EXPECT_EQ(f.order[f.order.size() - 2], "configuring");
    EXPECT_EQ(f.order.back(), "finished");
}

TEST(DownloadQueueManager, AFailingStepNeverUndoesTheInstall) {
    Fixture f;
    f.source.addSyntheticFile(kUrl, 1000);
    FakeStep broken("retroarch", Status(makeError(ErrorCode::PermissionDenied, "cfg read-only")));
    FakeStep cheats("cheats", success());
    f.downloads.addPostInstallStep(broken);
    f.downloads.addPostInstallStep(cheats);

    f.downloads.start(game(1000));

    ASSERT_EQ(f.finished.size(), 1u);
    EXPECT_TRUE(f.finished[0].result.ok());
    EXPECT_TRUE(f.fs.isFile("/roms/nds/Game.nds"));
    ASSERT_EQ(f.finished[0].steps.size(), 2u);  // later steps still run
    EXPECT_EQ(f.finished[0].steps[0].result.error().code, ErrorCode::PermissionDenied);
    EXPECT_TRUE(f.finished[0].steps[1].result.ok());
}

TEST(DownloadQueueManager, StepsDoNotRunWhenTheInstallFails) {
    Fixture f;
    f.source.addFile(kUrl, "123456789");
    FakeStep step("retroarch", success());
    f.downloads.addPostInstallStep(step);
    GameEntry corrupt = game(9);
    corrupt.crc32 = "00000000";

    f.downloads.start(corrupt);

    EXPECT_TRUE(step.ranFor.empty());
    EXPECT_TRUE(f.finished.at(0).steps.empty());
    EXPECT_EQ(std::count(f.order.begin(), f.order.end(), "configuring"), 0);
}

TEST(DownloadQueueManager, StepsSeeTheCrcMeasuredDuringTheDownload) {
    Fixture f;
    f.source.addFile(kUrl, "123456789");  // CRC-32 check value: cbf43926
    FakeStep playlist("playlist", success());
    f.downloads.addPostInstallStep(playlist);

    f.downloads.start(game(9));  // the index announced no CRC

    EXPECT_EQ(playlist.crcs, (std::vector<std::string>{"cbf43926"}));
}
