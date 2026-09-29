// Interrupted downloads: the partial .tmp is kept on a network failure and
// the next attempt asks the server for the missing bytes only (FTP REST).

#include <gtest/gtest.h>

#include <optional>

#include "MemoryFileSystem.hpp"
#include "retromanager/core/Crc32.hpp"
#include "retromanager/fs/FileInstall.hpp"
#include "retromanager/network/MockRemoteSource.hpp"
#include "retromanager/services/DownloadQueueManager.hpp"

using namespace rm;

namespace {

const std::string kUrl = "ftp://mock.local/shop/roms/nds/Game.nds";
const char* kRom = "/roms/nds/Game.nds";

std::string content(std::size_t size) {
    std::string data(size, '\0');
    for (std::size_t i = 0; i < size; ++i) data[i] = static_cast<char>('a' + (i * 7) % 26);
    return data;
}

std::string crcOf(const std::string& data) {
    Crc32 crc;
    crc.update(data.data(), data.size());
    return crc.hex();
}

GameEntry game(const std::string& data, bool withCrc = true) {
    GameEntry entry;
    entry.id = "nds/Game.nds";
    entry.title = "Game";
    entry.system = "nds";
    entry.fileName = "Game.nds";
    entry.sizeBytes = data.size();
    entry.romUrl = kUrl;
    if (withCrc) entry.crc32 = crcOf(data);
    return entry;
}

class NullSystem : public ISystem {
  public:
    void setKeepAwake(bool) override {}
};

struct Fixture {
    test::MemoryFileSystem fs;
    MockRemoteSource source{"{}", ""};
    RomStore store{fs, SdLayout{}};
    ImmediateTaskRunner mainThread;
    EventBus bus{mainThread};
    NullSystem system;
    DownloadQueueManager downloads{source, store, bus, system, std::make_unique<ImmediateTaskRunner>()};
    std::optional<DownloadFinished> finished;
    std::vector<DownloadProgressed> progress;
    EventBus::Subscription s1 = bus.subscribe<DownloadFinished>([this](const DownloadFinished& e) { finished = e; });
    EventBus::Subscription s2 = bus.subscribe<DownloadProgressed>([this](const DownloadProgressed& e) { progress.push_back(e); });

    Status download(const GameEntry& entry) {
        finished.reset();
        progress.clear();
        downloads.start(entry);
        EXPECT_TRUE(finished.has_value());
        return finished ? finished->result : Status(makeError(ErrorCode::IoError, "no event"));
    }
};

}  // namespace

// --- remote side -------------------------------------------------------------

TEST(RemoteResume, MockServesTheTailFromAnOffset) {
    MockRemoteSource source("{}", "");
    source.addFile(kUrl, "0123456789");
    std::string received;
    std::vector<TransferProgress> ticks;
    CancellationToken cancel;
    Status status = source.downloadFileFrom(
        kUrl, 4, [&](const char* d, std::size_t n) { received.append(d, n); return success(); },
        [&](const TransferProgress& p) { ticks.push_back(p); }, cancel);
    ASSERT_TRUE(status.ok()) << status.error().describe();
    EXPECT_EQ(received, "456789");
    ASSERT_FALSE(ticks.empty());
    EXPECT_EQ(ticks.back().received, 10u);  // progress counts the whole file
    EXPECT_EQ(ticks.back().total, 10u);
    EXPECT_EQ(source.lastOffset(), 4u);
}

TEST(RemoteResume, RefusalIsUnsupported) {
    MockRemoteSource source("{}", "");
    source.addFile(kUrl, "0123456789");
    CancellationToken cancel;
    auto sink = [](const char*, std::size_t) { return success(); };
    EXPECT_EQ(source.downloadFileFrom(kUrl, 20, sink, nullptr, cancel).error().code, ErrorCode::Unsupported);  // past the end
    source.setResumeSupported(false);
    EXPECT_EQ(source.downloadFileFrom(kUrl, 4, sink, nullptr, cancel).error().code, ErrorCode::Unsupported);
    EXPECT_TRUE(source.downloadFileFrom(kUrl, 0, sink, nullptr, cancel).ok());  // offset 0 is a plain download
}

TEST(RemoteResume, SourcesWithoutResumeSupportRefuseOffsets) {
    UnavailableRemoteSource unavailable(makeError(ErrorCode::NotConfigured, "x"), "x");
    CancellationToken cancel;
    auto sink = [](const char*, std::size_t) { return success(); };
    EXPECT_EQ(unavailable.downloadFileFrom(kUrl, 4, sink, nullptr, cancel).error().code, ErrorCode::Unsupported);
}

// --- FileInstall ---------------------------------------------------------------

TEST(FileInstallResume, ContinuesAPartialFileAndKeepsTheCrcRight) {
    test::MemoryFileSystem fs;
    const std::string data = content(5000);
    {
        auto first = beginFileInstall(fs, kRom, data.size(), crcOf(data), "Game");
        ASSERT_TRUE(first.ok());
        ASSERT_TRUE(first.value()->write(data.data(), 3000).ok());
        ASSERT_TRUE(first.value()->suspend().ok());
    }
    auto resumed = beginFileInstall(fs, kRom, data.size(), crcOf(data), "Game", /*resume=*/true);
    ASSERT_TRUE(resumed.ok()) << resumed.error().describe();
    EXPECT_EQ(resumed.value()->resumedFrom(), 3000u);
    EXPECT_EQ(resumed.value()->bytesWritten(), 3000u);
    ASSERT_TRUE(resumed.value()->write(data.data() + 3000, 2000).ok());
    ASSERT_TRUE(resumed.value()->commit().ok());  // CRC of the whole file, old bytes included
    EXPECT_EQ(fs.readFile(kRom).value(), data);
}

TEST(FileInstallResume, PartialLargerThanTheFileIsDiscarded) {
    test::MemoryFileSystem fs;
    {
        auto first = beginFileInstall(fs, kRom, 0, "", "Game");
        ASSERT_TRUE(first.value()->write("0123456789", 10).ok());
        ASSERT_TRUE(first.value()->suspend().ok());
    }
    auto resumed = beginFileInstall(fs, kRom, 5, "", "Game", true);  // the server's file is smaller now
    ASSERT_TRUE(resumed.ok());
    EXPECT_EQ(resumed.value()->resumedFrom(), 0u);
}

TEST(FileInstallResume, RestartStartsOverFromAnEmptyFile) {
    test::MemoryFileSystem fs;
    {
        auto first = beginFileInstall(fs, kRom, 0, "", "Game");
        ASSERT_TRUE(first.value()->write("stale", 5).ok());
        ASSERT_TRUE(first.value()->suspend().ok());
    }
    auto resumed = beginFileInstall(fs, kRom, 0, "", "Game", true);
    ASSERT_EQ(resumed.value()->resumedFrom(), 5u);
    ASSERT_TRUE(resumed.value()->restart().ok());  // the server refused REST
    EXPECT_EQ(resumed.value()->resumedFrom(), 0u);
    EXPECT_EQ(resumed.value()->bytesWritten(), 0u);
    ASSERT_TRUE(resumed.value()->write("fresh", 5).ok());
    ASSERT_TRUE(resumed.value()->commit().ok());
    EXPECT_EQ(fs.readFile(kRom).value(), "fresh");
}

TEST(FileInstallResume, APartialWithABadHeaderIsThrownAway) {
    test::MemoryFileSystem fs;
    {
        auto first = beginFileInstall(fs, "/switch/A/A.nro", 0, "", "A");
        ASSERT_TRUE(fs.createDirectories("/switch/A").ok());
        ASSERT_TRUE(first.value()->write("<html>error page", 16).ok());
        ASSERT_TRUE(first.value()->suspend().ok());
    }
    auto resumed = beginFileInstall(fs, "/switch/A/A.nro", 0, "", "A", true);
    ASSERT_EQ(resumed.value()->resumedFrom(), 16u);
    resumed.value()->setHeaderCheck(4, [](std::string_view h) {
        return h == "NRO0" ? success() : Status(makeError(ErrorCode::IntegrityError, "bad"));
    });
    EXPECT_EQ(resumed.value()->resumedFrom(), 0u);  // restarted: nothing to salvage
    ASSERT_TRUE(resumed.value()->write("NRO0body", 8).ok());
    ASSERT_TRUE(resumed.value()->commit().ok());
}

// --- the whole pipeline ----------------------------------------------------------

TEST(DownloadResume, ANetworkFailureKeepsThePartialFileAndTheRetryOnlyFetchesTheRest) {
    Fixture f;
    const std::string data = content(200 * 1024);
    f.source.addFile(kUrl, data);
    f.source.setDownloadFailure(64 * 1024, makeError(ErrorCode::NetworkError, "Wi-Fi lost"));

    EXPECT_EQ(f.download(game(data)).error().code, ErrorCode::NetworkError);
    EXPECT_FALSE(f.fs.exists(kRom));
    auto partial = f.fs.stat(stagingPath(kRom));
    ASSERT_TRUE(partial.ok()) << "the partial download must survive a network failure";
    EXPECT_EQ(partial.value().size, 64u * 1024);

    f.source.clearDownloadFailure();
    Status retried = f.download(game(data));
    ASSERT_TRUE(retried.ok()) << retried.error().describe();
    EXPECT_EQ(f.source.lastOffset(), 64u * 1024);  // REST 65536
    EXPECT_EQ(f.fs.readFile(kRom).value(), data);   // CRC checked over the whole file
    EXPECT_FALSE(f.fs.exists(stagingPath(kRom)));
    ASSERT_FALSE(f.progress.empty());
    EXPECT_EQ(f.progress.front().resumedFrom, 64u * 1024);
    EXPECT_EQ(f.progress.back().received, data.size());
}

TEST(DownloadResume, WhenTheServerRefusesTheFileIsDownloadedAgainFromScratch) {
    Fixture f;
    const std::string data = content(100 * 1024);
    f.source.addFile(kUrl, data);
    f.source.setDownloadFailure(30 * 1024, makeError(ErrorCode::NetworkError, "drop"));
    EXPECT_FALSE(f.download(game(data)).ok());
    f.source.clearDownloadFailure();
    f.source.setResumeSupported(false);

    Status retried = f.download(game(data));
    ASSERT_TRUE(retried.ok()) << retried.error().describe();
    EXPECT_EQ(f.source.lastOffset(), 0u);
    EXPECT_EQ(f.fs.readFile(kRom).value(), data);
}

TEST(DownloadResume, ACorruptPartialFileFailsTheCrcAndIsNotKept) {
    Fixture f;
    const std::string data = content(50 * 1024);
    f.source.addFile(kUrl, data);
    f.source.setDownloadFailure(20 * 1024, makeError(ErrorCode::NetworkError, "drop"));
    EXPECT_FALSE(f.download(game(data)).ok());
    f.source.clearDownloadFailure();
    // The NAS file changed in the meantime: same size, other content.
    std::string other = content(50 * 1024);
    other[0] = '#';
    other[40000] = '#';
    f.source.addFile(kUrl, other);

    EXPECT_EQ(f.download(game(other)).error().code, ErrorCode::IntegrityError);  // old head + new tail
    EXPECT_FALSE(f.fs.exists(stagingPath(kRom)));                               // not resumed again
    EXPECT_TRUE(f.download(game(other)).ok());                                   // clean retry works
}

TEST(DownloadResume, UserCancellationDeletesThePartialFile) {
    Fixture f;
    const std::string data = content(100 * 1024);
    f.source.addFile(kUrl, data);
    f.source.setChunkSize(1024);
    f.source.setThroughput(200 * 1024);  // ~0.5 s: progress events (10 per second) get a chance
    EventBus::Subscription cancelHalfway = f.bus.subscribe<DownloadProgressed>([&](const DownloadProgressed& e) {
        if (e.received >= 10 * 1024) f.downloads.cancel(e.id);
    });
    EXPECT_EQ(f.download(game(data)).error().code, ErrorCode::Cancelled);
    EXPECT_FALSE(f.fs.exists(stagingPath(kRom)));
}

TEST(DownloadResume, AnAlreadyCompletePartialFileIsJustVerified) {
    Fixture f;
    const std::string data = content(10 * 1024);
    f.source.addFile(kUrl, data);
    {
        auto partial = f.store.beginInstall(game(data));
        ASSERT_TRUE(partial.value()->write(data.data(), data.size()).ok());
        ASSERT_TRUE(partial.value()->suspend().ok());  // e.g. power cut right before the rename
    }
    ASSERT_TRUE(f.download(game(data)).ok());
    EXPECT_EQ(f.source.downloadCount(), 0);  // nothing left to fetch
    EXPECT_EQ(f.fs.readFile(kRom).value(), data);
}
