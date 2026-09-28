#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <deque>
#include <mutex>
#include <random>
#include <set>
#include <thread>

#include "MemoryFileSystem.hpp"
#include "MockSdCard.hpp"
#include "retromanager/core/EventBus.hpp"
#include "retromanager/core/Md5.hpp"
#include "retromanager/core/WorkerThread.hpp"
#include "retromanager/models/Bios.hpp"
#include "retromanager/network/MockRemoteSource.hpp"
#include "retromanager/parsers/CfgDocument.hpp"
#include "retromanager/parsers/RepoIndexParser.hpp"
#include "retromanager/services/BiosManager.hpp"

using namespace rm;

// --- MD5 -----------------------------------------------------------------

TEST(Md5, MatchesTheRfc1321TestSuite) {
    EXPECT_EQ(Md5::of(""), "d41d8cd98f00b204e9800998ecf8427e");
    EXPECT_EQ(Md5::of("a"), "0cc175b9c0f1b6a831c399e269772661");
    EXPECT_EQ(Md5::of("abc"), "900150983cd24fb0d6963f7d28e17f72");
    EXPECT_EQ(Md5::of("message digest"), "f96b697d7cb7938d525a2f31aaf161d0");
    EXPECT_EQ(Md5::of("abcdefghijklmnopqrstuvwxyz"), "c3fcd3d76192e4007dfb496cca67e13b");
    EXPECT_EQ(Md5::of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789"),
              "d174ab98d277d9f5a5611c2c9f419d9f");
    EXPECT_EQ(Md5::of("12345678901234567890123456789012345678901234567890123456789012345678901234567890"),
              "57edf4a22be3c955ac49da2e2107b67a");
}

TEST(Md5, StreamingGivesTheSameDigestWhateverTheChunks) {
    std::mt19937 rng(42);
    std::string data(300000, '\0');
    for (char& c : data) c = static_cast<char>(rng() & 0xFF);
    const std::string expected = Md5::of(data);
    for (std::size_t chunk : {1u, 55u, 56u, 63u, 64u, 65u, 4096u}) {
        Md5 md5;
        for (std::size_t at = 0; at < data.size(); at += chunk) md5.update(data.data() + at, std::min(chunk, data.size() - at));
        EXPECT_EQ(md5.hex(), expected) << "chunk " << chunk;
    }
}

// --- catalogue -------------------------------------------------------------

TEST(BiosCatalogue, KnowsTheUsualRetroArchFiles) {
    auto gba = bios::forSystem("gba");
    ASSERT_EQ(gba.size(), 1u);
    EXPECT_EQ(gba[0]->fileName, "gba_bios.bin");
    EXPECT_EQ(gba[0]->md5, "a860e8c0b6d573d191e4ec7db1b1e4f6");

    auto psx = bios::forSystem("psx");
    auto scph5501 = std::find_if(psx.begin(), psx.end(), [](const BiosFile* f) { return f->fileName == "scph5501.bin"; });
    ASSERT_NE(scph5501, psx.end());
    EXPECT_TRUE((*scph5501)->required);
    EXPECT_TRUE(bios::forSystem("snes").empty());
    EXPECT_NE(bios::find("SCPH5501.BIN"), nullptr);  // FAT: case-insensitive
}

TEST(BiosCatalogue, IsWellFormed) {
    std::set<std::string> names;
    for (const BiosFile& file : bios::catalogue()) {
        EXPECT_TRUE(names.insert(file.fileName).second) << "duplicate " << file.fileName;
        EXPECT_TRUE(file.md5.empty() || Md5::isDigest(file.md5)) << file.fileName;
        EXPECT_FALSE(file.system.empty());
        EXPECT_TRUE(bios::isSafeFileName(file.fileName)) << file.fileName;
    }
}

TEST(BiosCatalogue, SafeFileNames) {
    EXPECT_TRUE(bios::isSafeFileName("scph5501.bin"));
    EXPECT_TRUE(bios::isSafeFileName("neogeo.zip"));
    EXPECT_FALSE(bios::isSafeFileName(""));
    EXPECT_FALSE(bios::isSafeFileName("../retroarch.cfg"));
    EXPECT_FALSE(bios::isSafeFileName("dc/dc_boot.bin"));  // subfolders: not supported
    EXPECT_FALSE(bios::isSafeFileName("a\\b.bin"));
    EXPECT_FALSE(bios::isSafeFileName(".hidden"));
    EXPECT_FALSE(bios::isSafeFileName(".."));
}

// --- "bios" section of the shop index --------------------------------------

TEST(RepoIndexBios, ParsesTheBiosSection) {
    auto index = RepoIndexParser("ftp://nas.local/shop/index.json").parse(R"({
        "games": [],
        "bios": [
            {"file": "scph5501.bin", "system": "psx", "url": "bios/scph5501.bin",
             "md5": "490F666E1AFB15B7362B406ED1CEA246", "size": 524288},
            {"url": "bios/gba_bios.bin"},
            {"file": "neogeo.zip", "system": "arcade", "url": "ftp://nas.local/bios/neogeo.zip"}
        ]
    })");
    ASSERT_TRUE(index.ok()) << index.error().describe();
    const auto& b = index.value().bios;
    ASSERT_EQ(b.size(), 3u);
    EXPECT_EQ(b[0].fileName, "scph5501.bin");
    EXPECT_EQ(b[0].system, "psx");
    EXPECT_EQ(b[0].url, "ftp://nas.local/shop/bios/scph5501.bin");
    EXPECT_EQ(b[0].md5, "490f666e1afb15b7362b406ed1cea246");  // normalized
    EXPECT_EQ(b[0].sizeBytes, 524288u);
    EXPECT_EQ(b[1].fileName, "gba_bios.bin");  // from the URL
    EXPECT_EQ(b[1].system, "gba");             // from the catalogue
    EXPECT_EQ(b[2].system, "arcade");
    EXPECT_TRUE(index.value().warnings.empty());
}

TEST(RepoIndexBios, BadEntriesAreSkippedNotFatal) {
    auto index = RepoIndexParser("ftp://nas.local/shop/").parse(R"({
        "games": [],
        "bios": [
            {"file": "../../retroarch/retroarch.cfg", "url": "evil.cfg"},
            {"file": "a.bin"},
            {"file": "b.bin", "url": "b.bin", "md5": "not-an-md5"},
            {"file": "c.bin", "url": "c.bin", "size": -1},
            {"file": "d.bin", "url": "d.bin"},
            {"file": "D.BIN", "url": "d2.bin"},
            42
        ]
    })");
    ASSERT_TRUE(index.ok()) << index.error().describe();
    ASSERT_EQ(index.value().bios.size(), 1u);
    EXPECT_EQ(index.value().bios[0].fileName, "d.bin");
    EXPECT_EQ(index.value().bios[0].system, "");  // unknown to the catalogue, no "system"
    EXPECT_EQ(index.value().warnings.size(), 6u);

    auto notArray = RepoIndexParser().parse(R"({"games": [], "bios": {}})");
    ASSERT_TRUE(notArray.ok());  // the shop still works
    EXPECT_EQ(notArray.value().warnings.size(), 1u);
}

// --- BiosManager -------------------------------------------------------------

namespace {

const std::string kFake = "fake bios bytes";
const std::string kOther = "another dump";

// A small catalogue with digests the tests can produce.
std::vector<BiosFile> testCatalogue() {
    return {
        {"gba", "gba_bios.bin", Md5::of(kFake), "Game Boy Advance BIOS", false},
        {"psx", "scph5501.bin", Md5::of(kFake), "PlayStation BIOS (USA)", true},
        {"psx", "scph5502.bin", Md5::of(kFake), "PlayStation BIOS (Europe)", false},
        {"nds", "firmware.bin", "", "Nintendo DS firmware", false},
    };
}

BiosEntry offer(std::string file, std::string system, std::string md5 = "") {
    BiosEntry entry;
    entry.fileName = file;
    entry.system = std::move(system);
    entry.url = "ftp://mock.local/shop/bios/" + file;
    entry.md5 = std::move(md5);
    return entry;
}

const BiosStatus* row(const std::vector<BiosStatus>& rows, const std::string& file) {
    for (const BiosStatus& r : rows) {
        if (r.fileName == file) return &r;
    }
    return nullptr;
}

struct Fixture {
    std::unique_ptr<test::MemoryFileSystem> sd = test::makeMockSdCard();
    MockRemoteSource shop{"{}", ""};
    ImmediateTaskRunner mainThread;
    EventBus bus{mainThread};
    BiosManager bios{*sd, SdLayout{}, shop, bus, std::make_unique<ImmediateTaskRunner>(), testCatalogue()};
    CancellationToken cancel;
};

}  // namespace

TEST(BiosManager, ReportsEveryKnownFileWithItsState) {
    Fixture f;
    ASSERT_TRUE(f.sd->writeFile("/retroarch/system/gba_bios.bin", kFake).ok());
    ASSERT_TRUE(f.sd->writeFile("/retroarch/system/SCPH5502.BIN", kOther).ok());  // other dump, other case
    ASSERT_TRUE(f.sd->writeFile("/retroarch/system/firmware.bin", "x").ok());

    auto rows = f.bios.check({offer("scph5501.bin", "psx")});
    ASSERT_EQ(rows.size(), 4u);
    EXPECT_EQ(row(rows, "gba_bios.bin")->state, BiosState::Ok);
    EXPECT_EQ(row(rows, "scph5501.bin")->state, BiosState::Missing);
    EXPECT_TRUE(row(rows, "scph5501.bin")->required);
    ASSERT_TRUE(row(rows, "scph5501.bin")->offer.has_value());
    EXPECT_FALSE(row(rows, "gba_bios.bin")->offer.has_value());
    EXPECT_EQ(row(rows, "scph5502.bin")->state, BiosState::Unrecognized);  // present, unknown digest
    EXPECT_EQ(row(rows, "scph5502.bin")->path, "/retroarch/system/SCPH5502.BIN");
    EXPECT_EQ(row(rows, "firmware.bin")->state, BiosState::Unverified);  // no reference digest
    EXPECT_EQ(row(rows, "gba_bios.bin")->path, "/retroarch/system/gba_bios.bin");
}

TEST(BiosManager, ListsExtraFilesOfferedByTheShop) {
    Fixture f;
    auto rows = f.bios.check({offer("neogeo.zip", "arcade", Md5::of(kFake))});
    const BiosStatus* neogeo = row(rows, "neogeo.zip");
    ASSERT_NE(neogeo, nullptr);
    EXPECT_EQ(neogeo->system, "arcade");
    EXPECT_EQ(neogeo->state, BiosState::Missing);

    ASSERT_TRUE(f.sd->writeFile("/retroarch/system/neogeo.zip", kFake).ok());
    EXPECT_EQ(row(f.bios.check({offer("neogeo.zip", "arcade", Md5::of(kFake))}), "neogeo.zip")->state, BiosState::Ok);
}

TEST(BiosManager, RowsAreGroupedBySystem) {
    Fixture f;
    auto rows = f.bios.check({});
    std::vector<std::string> systems;
    for (const BiosStatus& r : rows) systems.push_back(r.system);
    EXPECT_TRUE(std::is_sorted(systems.begin(), systems.end()));
}

TEST(BiosManager, InstallsIntoRetroArchsSystemFolder) {
    Fixture f;
    f.shop.addFile("ftp://mock.local/shop/bios/scph5501.bin", kFake);
    f.shop.setChunkSize(4);  // streamed

    Status installed = f.bios.installNow(offer("scph5501.bin", "psx", Md5::of(kFake)), f.cancel);
    ASSERT_TRUE(installed.ok()) << installed.error().describe();
    EXPECT_EQ(f.sd->readFile("/retroarch/system/scph5501.bin").value(), kFake);
    EXPECT_EQ(row(f.bios.check({}), "scph5501.bin")->state, BiosState::Ok);
}

TEST(BiosManager, WrongDigestFromTheShopInstallsNothing) {
    Fixture f;
    f.shop.addFile("ftp://mock.local/shop/bios/scph5501.bin", kOther);
    Status installed = f.bios.installNow(offer("scph5501.bin", "psx", Md5::of(kFake)), f.cancel);
    EXPECT_EQ(installed.error().code, ErrorCode::IntegrityError);
    EXPECT_FALSE(f.sd->exists("/retroarch/system/scph5501.bin"));
    EXPECT_TRUE(f.sd->listDirectory("/retroarch/system").ok());
    for (const DirEntry& e : f.sd->listDirectory("/retroarch/system").value()) EXPECT_NE(e.name, ".scph5501.bin.tmp");
}

TEST(BiosManager, UnknownDumpIsInstalledButFlagged) {
    // No digest from the shop: the catalogue's is only a reference (other
    // regions, revisions...). The file is installed and shown as unrecognized.
    Fixture f;
    f.shop.addFile("ftp://mock.local/shop/bios/scph5501.bin", kOther);
    ASSERT_TRUE(f.bios.installNow(offer("scph5501.bin", "psx"), f.cancel).ok());
    EXPECT_EQ(row(f.bios.check({}), "scph5501.bin")->state, BiosState::Unrecognized);
}

TEST(BiosManager, ReplacesAFileWithAnotherCaseInPlace) {
    Fixture f;
    ASSERT_TRUE(f.sd->writeFile("/retroarch/system/SCPH5501.BIN", kOther).ok());
    f.shop.addFile("ftp://mock.local/shop/bios/scph5501.bin", kFake);
    ASSERT_TRUE(f.bios.installNow(offer("scph5501.bin", "psx"), f.cancel).ok());
    EXPECT_EQ(f.sd->readFile("/retroarch/system/SCPH5501.BIN").value(), kFake);
    // MemoryFileSystem is case-sensitive: a second, lowercase file would show here.
    EXPECT_FALSE(f.sd->exists("/retroarch/system/scph5501.bin"));
}

TEST(BiosManager, CancelledOrFailedDownloadLeavesNothing) {
    Fixture f;
    f.shop.addFile("ftp://mock.local/shop/bios/scph5501.bin", kFake);
    f.cancel.cancel();
    EXPECT_EQ(f.bios.installNow(offer("scph5501.bin", "psx"), f.cancel).error().code, ErrorCode::Cancelled);
    EXPECT_FALSE(f.sd->exists("/retroarch/system/scph5501.bin"));

    CancellationToken fresh;
    EXPECT_EQ(f.bios.installNow(offer("missing.bin", "psx"), fresh).error().code, ErrorCode::NotFound);
    EXPECT_FALSE(f.sd->exists("/retroarch/system/missing.bin"));
}

TEST(BiosManager, RefusesUnsafeNamesAndNeedsRetroArch) {
    Fixture f;
    BiosEntry evil = offer("x.bin", "psx");
    evil.fileName = "../retroarch.cfg";
    EXPECT_EQ(f.bios.installNow(evil, f.cancel).error().code, ErrorCode::PermissionDenied);

    test::MemoryFileSystem empty;
    MockRemoteSource shop("{}", "");
    ImmediateTaskRunner runner;
    EventBus bus(runner);
    BiosManager noRetroArch(empty, SdLayout{}, shop, bus, std::make_unique<ImmediateTaskRunner>(), testCatalogue());
    EXPECT_EQ(noRetroArch.installNow(offer("scph5501.bin", "psx"), f.cancel).error().code, ErrorCode::NotFound);
    EXPECT_EQ(empty.nodeCount(), 0u);
}

TEST(BiosManager, CreatesTheSystemFolderAndHonoursSystemDirectory) {
    Fixture f;
    CfgDocument cfg = CfgDocument::parse(f.sd->readFile("/retroarch/retroarch.cfg").value());
    ASSERT_TRUE(cfg.set("system_directory", "/retroarch/cores/system").ok());
    ASSERT_TRUE(f.sd->writeFile("/retroarch/retroarch.cfg", cfg.serialize()).ok());
    f.shop.addFile("ftp://mock.local/shop/bios/scph5501.bin", kFake);

    EXPECT_EQ(f.bios.systemDirectory(), "/retroarch/cores/system");
    ASSERT_TRUE(f.bios.installNow(offer("scph5501.bin", "psx"), f.cancel).ok());
    EXPECT_TRUE(f.sd->isFile("/retroarch/cores/system/scph5501.bin"));
}

TEST(BiosManager, InstallsSeveralInTheBackgroundAndReportsEachOne) {
    Fixture f;
    f.shop.addFile("ftp://mock.local/shop/bios/scph5501.bin", kFake);
    f.shop.addFile("ftp://mock.local/shop/bios/gba_bios.bin", kFake);
    std::vector<BiosInstalled> each;
    std::vector<BiosInstallFinished> done;
    auto s1 = f.bus.subscribe<BiosInstalled>([&](const BiosInstalled& e) { each.push_back(e); });
    auto s2 = f.bus.subscribe<BiosInstallFinished>([&](const BiosInstallFinished& e) { done.push_back(e); });

    ASSERT_TRUE(f.bios.startInstall({offer("scph5501.bin", "psx"), offer("nope.bin", "psx"), offer("gba_bios.bin", "gba")}));

    ASSERT_EQ(each.size(), 3u);
    EXPECT_EQ(each[0].fileName, "scph5501.bin");
    EXPECT_TRUE(each[0].result.ok());
    EXPECT_EQ(each[1].result.error().code, ErrorCode::NotFound);  // one failure does not stop the others
    EXPECT_TRUE(each[2].result.ok());
    ASSERT_EQ(done.size(), 1u);
    EXPECT_EQ(done[0].installed, 2);
    EXPECT_EQ(done[0].failed, 1);
    EXPECT_FALSE(f.bios.running());
}

TEST(BiosManager, RunsOnItsWorkerAndJoinsOnDestruction) {
    auto sd = test::makeMockSdCard();
    MockRemoteSource shop("{}", "");
    shop.addSyntheticFile("ftp://mock.local/shop/bios/big.bin", 64 * 1024 * 1024);
    shop.setThroughput(32 * 1024 * 1024);  // ~2 s if left alone
    shop.addFile("ftp://mock.local/shop/bios/scph5501.bin", kFake);

    // Events are queued for the "main thread" (this test), like brls::sync.
    std::mutex mutex;
    std::deque<std::function<void()>> queue;
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
    } mainThread(mutex, queue);
    EventBus bus(mainThread);
    std::vector<std::string> installed;
    auto sub = bus.subscribe<BiosInstalled>([&](const BiosInstalled& e) { installed.push_back(e.fileName); });
    auto drain = [&] {
        std::deque<std::function<void()>> tasks;
        {
            std::lock_guard<std::mutex> lock(mutex);
            tasks.swap(queue);
        }
        for (auto& task : tasks) task();
    };

    auto begin = std::chrono::steady_clock::now();
    {
        BiosManager bios(*sd, SdLayout{}, shop, bus,
                         std::make_unique<WorkerThread>([&](std::function<void()> t) { mainThread.runOnMainThread(t); }),
                         testCatalogue());
        ASSERT_TRUE(bios.startInstall({offer("scph5501.bin", "psx"), offer("big.bin", "psx")}));
        EXPECT_FALSE(bios.startInstall({offer("scph5501.bin", "psx")}));  // one batch at a time
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        drain();
        EXPECT_EQ(installed, (std::vector<std::string>{"scph5501.bin"}));  // the first file is done
    }  // destructor cancels the big one and joins
    EXPECT_LT(std::chrono::steady_clock::now() - begin, std::chrono::milliseconds(1500));
    EXPECT_TRUE(sd->isFile("/retroarch/system/scph5501.bin"));
    EXPECT_FALSE(sd->exists("/retroarch/system/big.bin"));
}
