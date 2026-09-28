#include <gtest/gtest.h>

#include "MemoryFileSystem.hpp"
#include "MockSdCard.hpp"
#include "retromanager/network/MockRemoteSource.hpp"
#include "retromanager/parsers/CfgDocument.hpp"
#include "retromanager/services/CloudSyncService.hpp"

using namespace rm;

namespace {

const std::string kNas = "ftp://mock.local/Saves/";
const std::string kLocal = "/retroarch/saves/";
const std::string kMario = "Super Mario World (USA).srm";  // present in the fixture SD card

class FakeSystem : public ISystem {
  public:
    void setKeepAwake(bool keepAwake) override { calls.push_back(keepAwake); }
    std::vector<bool> calls;
};

// Mock SD card + mock NAS with controllable clocks: the console and the NAS
// deliberately disagree on the time, like real devices.
struct Fixture {
    std::unique_ptr<test::MemoryFileSystem> sd = test::makeMockSdCard();
    MockRemoteSource nas{"{}", ""};
    std::int64_t consoleTime = 1'000'000;
    std::int64_t nasTime = 1'000'000 + 3600;  // one hour ahead
    ImmediateTaskRunner mainThread;
    EventBus bus{mainThread};
    FakeSystem system;
    std::unique_ptr<CloudSyncService> sync;

    Fixture() {
        sd->setClock([this] { return consoleTime; });
        nas.setClock([this] { return nasTime; });
        // The fixture's save was written "now" by makeMockSdCard: pin it.
        sd->setModificationTime(kLocal + kMario, consoleTime).ok();
        sync = std::make_unique<CloudSyncService>(*sd, SdLayout{}, nas, kNas, bus, system,
                                                  std::make_unique<ImmediateTaskRunner>(), [this] { return consoleTime; });
    }

    SyncReport run() {
        CancellationToken cancel;
        auto report = sync->syncNow(cancel);
        EXPECT_TRUE(report.ok()) << (report.ok() ? "" : report.error().describe());
        return report.ok() ? report.value() : SyncReport{};
    }

    void writeLocal(const std::string& relative, const std::string& content) {
        ASSERT_TRUE(sd->createDirectories(vpathParent(kLocal + relative)).ok());
        ASSERT_TRUE(sd->writeFile(kLocal + relative, content).ok());
    }
    void writeNas(const std::string& relative, const std::string& content) { nas.addFile(kNas + relative, content); }
    std::string local(const std::string& relative) { return sd->readFile(kLocal + relative).valueOr("<missing>"); }
    std::string remote(const std::string& relative) { return nas.fileContent(kNas + relative).value_or("<missing>"); }

    static std::string vpathParent(const std::string& path) { return path.substr(0, path.rfind('/')); }
};

}  // namespace

TEST(CloudSync, FirstSyncUploadsLocalSaves) {
    Fixture f;
    SyncReport report = f.run();
    EXPECT_EQ(report.uploaded, 1);
    EXPECT_EQ(report.downloaded, 0);
    EXPECT_EQ(f.remote(kMario), "MOCK SAVE\n");
    EXPECT_TRUE(report.failures.empty());
}

TEST(CloudSync, DownloadsNasOnlySavesIncludingSubfolders) {
    Fixture f;
    f.writeNas("Pokemon%20Platine%20(France).sav", "DS SAVE");
    f.writeNas("mGBA/Advance%20Wars%20(USA).srm", "GBA SAVE");

    SyncReport report = f.run();
    EXPECT_EQ(report.downloaded, 2);
    EXPECT_EQ(f.local("Pokemon Platine (France).sav"), "DS SAVE");
    EXPECT_EQ(f.local("mGBA/Advance Wars (USA).srm"), "GBA SAVE");
    for (const DirEntry& e : f.sd->listDirectory("/retroarch/saves").value()) EXPECT_FALSE(isStagingName(e.name));
}

TEST(CloudSync, ASecondSyncTransfersNothing) {
    // The NAS stamps the upload with its own (later) clock: the naive
    // "newest wins" rule would now download the save back, and so on forever.
    Fixture f;
    f.writeNas("Other.srm", "from nas");
    SyncReport first = f.run();
    ASSERT_EQ(first.uploaded + first.downloaded, 2);

    f.consoleTime += 60;
    f.nasTime += 60;
    SyncReport second = f.run();
    EXPECT_EQ(second.uploaded, 0);
    EXPECT_EQ(second.downloaded, 0);
    EXPECT_EQ(second.unchanged, 2);
    EXPECT_EQ(f.nas.uploadCount(), 1);
}

TEST(CloudSync, PropagatesALocalChange) {
    Fixture f;
    f.run();
    f.consoleTime += 600;  // played on the console
    f.writeLocal(kMario, "LEVEL 2");

    SyncReport report = f.run();
    EXPECT_EQ(report.uploaded, 1);
    EXPECT_EQ(f.remote(kMario), "LEVEL 2");
}

TEST(CloudSync, PropagatesANasChange) {
    Fixture f;
    f.run();
    f.nasTime += 600;  // played elsewhere, synced to the NAS by another device
    f.writeNas("Super%20Mario%20World%20(USA).srm", "LEVEL 3");

    SyncReport report = f.run();
    EXPECT_EQ(report.downloaded, 1);
    EXPECT_EQ(f.local(kMario), "LEVEL 3");
}

TEST(CloudSync, ConflictKeepsTheNewestAndSavesTheOther) {
    Fixture f;
    f.run();
    f.nasTime += 100;
    f.writeNas("Super%20Mario%20World%20(USA).srm", "NAS VERSION");
    f.consoleTime += 5000;  // the console played later
    f.writeLocal(kMario, "CONSOLE VERSION");

    SyncReport report = f.run();
    EXPECT_EQ(report.conflicts, 1);
    EXPECT_EQ(f.remote(kMario), "CONSOLE VERSION");  // newest wins...
    ASSERT_EQ(report.conflictCopies.size(), 1u);     // ...the other one is kept
    EXPECT_EQ(f.sd->readFile(report.conflictCopies[0]).value(), "NAS VERSION");
    EXPECT_NE(report.conflictCopies[0].find(".conflict-remote-"), std::string::npos) << report.conflictCopies[0];

    // The copy is not a save file: it is never synced nor picked by RetroArch.
    EXPECT_FALSE(CloudSyncService::isSaveFile(report.conflictCopies[0]));
    SyncReport again = f.run();
    EXPECT_EQ(again.uploaded + again.downloaded + again.conflicts, 0);
}

TEST(CloudSync, ConflictWonByTheNasKeepsTheConsoleVersion) {
    Fixture f;
    f.run();
    f.consoleTime += 100;
    f.writeLocal(kMario, "CONSOLE VERSION");
    f.nasTime += 999999;
    f.writeNas("Super%20Mario%20World%20(USA).srm", "NAS VERSION");

    SyncReport report = f.run();
    EXPECT_EQ(report.conflicts, 1);
    EXPECT_EQ(f.local(kMario), "NAS VERSION");
    ASSERT_EQ(report.conflictCopies.size(), 1u);
    EXPECT_EQ(f.sd->readFile(report.conflictCopies[0]).value(), "CONSOLE VERSION");
}

TEST(CloudSync, FirstMeetingOfIdenticalFilesTransfersNothing) {
    Fixture f;
    f.nasTime = f.consoleTime + 1;  // same save copied by hand, FAT 2 s rounding
    f.writeNas("Super%20Mario%20World%20(USA).srm", "MOCK SAVE\n");
    SyncReport report = f.run();
    EXPECT_EQ(report.unchanged, 1);
    EXPECT_EQ(f.nas.uploadCount(), 0);
}

TEST(CloudSync, IgnoresNonSaveFilesAndStagingLeftovers) {
    Fixture f;
    f.writeLocal("Game.state", "savestate");        // not a save
    f.writeLocal("notes.txt", "hello");
    f.writeNas(".Game.srm.tmp", "half upload");     // interrupted upload on the NAS
    f.writeNas("Game.srm.conflict-local-x", "old");  // conflict copy
    SyncReport report = f.run();
    EXPECT_EQ(report.uploaded, 1);  // only the fixture's .srm
    EXPECT_EQ(report.downloaded, 0);
    EXPECT_FALSE(f.nas.fileContent(kNas + "Game.state").has_value());
}

TEST(CloudSync, OneFailingFileDoesNotStopTheOthers) {
    Fixture f;
    f.writeNas("Blocked.srm", "nas");
    f.writeNas("Fine.srm", "nas");
    // A directory with the save's name makes that one download impossible.
    ASSERT_TRUE(f.sd->createDirectories(kLocal + "Blocked.srm").ok());

    SyncReport report = f.run();
    ASSERT_EQ(report.failures.size(), 1u);
    EXPECT_EQ(report.failures[0].first, "Blocked.srm");
    EXPECT_EQ(f.local("Fine.srm"), "nas");
    EXPECT_EQ(report.uploaded, 1);
    EXPECT_EQ(report.downloaded, 1);

    // The failed file is retried next time, the others are not re-sent.
    ASSERT_TRUE(f.sd->removeAll(kLocal + "Blocked.srm").ok());
    SyncReport retry = f.run();
    EXPECT_EQ(retry.downloaded, 1);
    EXPECT_EQ(retry.uploaded, 0);
}

TEST(CloudSync, UnreachableNasFailsTheWholeSyncCleanly) {
    Fixture f;
    f.nas.setFailure(makeError(ErrorCode::NetworkError, "NAS is off"));
    CancellationToken cancel;
    auto report = f.sync->syncNow(cancel);
    ASSERT_FALSE(report.ok());
    EXPECT_EQ(report.error().code, ErrorCode::NetworkError);
    EXPECT_FALSE(f.sd->exists(f.sync->syncStatePath()));  // nothing recorded
}

TEST(CloudSync, UsesRetroArchSavefileDirectory) {
    Fixture f;
    CfgDocument cfg = CfgDocument::parse(f.sd->readFile("/retroarch/retroarch.cfg").value());
    ASSERT_TRUE(cfg.set("savefile_directory", "/saves/custom").ok());
    ASSERT_TRUE(f.sd->writeFile("/retroarch/retroarch.cfg", cfg.serialize()).ok());
    EXPECT_EQ(f.sync->localSavesDirectory(), "/saves/custom");

    f.writeNas("x.srm", "nas");
    f.run();
    EXPECT_EQ(f.sd->readFile("/saves/custom/x.srm").valueOr(""), "nas");
}

TEST(CloudSync, StateOfAnotherNasFolderIsNotReused) {
    Fixture f;
    f.run();
    MockRemoteSource otherNas("{}", "");
    CloudSyncService other(*f.sd, SdLayout{}, otherNas, "ftp://mock.local/OtherSaves/", f.bus, f.system,
                           std::make_unique<ImmediateTaskRunner>());
    CancellationToken cancel;
    auto report = other.syncNow(cancel);
    ASSERT_TRUE(report.ok());
    EXPECT_EQ(report.value().uploaded, 1);  // treated as a first sync
}

TEST(CloudSync, CorruptedStateFileIsTreatedAsAFirstSync) {
    Fixture f;
    ASSERT_TRUE(f.sd->writeFile(f.sync->syncStatePath(), "{ not json").ok());
    SyncReport report = f.run();
    EXPECT_EQ(report.uploaded, 1);
    EXPECT_TRUE(parseSyncState(f.sd->readFile(f.sync->syncStatePath()).value()).ok());  // rewritten
}

TEST(CloudSync, CancellationStopsAndKeepsWhatWasDone) {
    Fixture f;
    for (int i = 0; i < 5; ++i) f.writeNas("n" + std::to_string(i) + ".srm", "x");
    CancellationToken cancel;
    int seen = 0;
    auto report = f.sync->syncNow(cancel, [&](const SyncProgressed&) {
        if (++seen == 2) cancel.cancel();
    });
    ASSERT_FALSE(report.ok());
    EXPECT_EQ(report.error().code, ErrorCode::Cancelled);

    SyncReport rest = f.run();  // what was done is recorded, the rest follows
    EXPECT_LT(rest.downloaded + rest.uploaded, 6);
    EXPECT_GT(rest.downloaded + rest.uploaded, 0);
    EXPECT_EQ(f.run().downloaded + f.run().uploaded, 0);
}

TEST(CloudSync, BackgroundRunReportsProgressAndResultAndKeepsTheConsoleAwake) {
    Fixture f;
    f.writeNas("Other.srm", "nas");
    std::vector<SyncProgressed> progress;
    std::optional<SyncFinished> finished;
    auto p = f.bus.subscribe<SyncProgressed>([&](const SyncProgressed& e) { progress.push_back(e); });
    auto d = f.bus.subscribe<SyncFinished>([&](const SyncFinished& e) { finished = e; });

    ASSERT_TRUE(f.sync->start());

    ASSERT_TRUE(finished.has_value());
    ASSERT_TRUE(finished->result.ok());
    EXPECT_EQ(finished->report.uploaded, 1);
    EXPECT_EQ(finished->report.downloaded, 1);
    ASSERT_EQ(progress.size(), 2u);
    EXPECT_EQ(progress.back().done, 2);
    EXPECT_EQ(progress.back().total, 2);
    EXPECT_EQ(f.system.calls, (std::vector<bool>{true, false}));
    EXPECT_FALSE(f.sync->running());
}

TEST(CloudSync, NotConfiguredIsReportedWithoutTouchingAnything) {
    Fixture f;
    CloudSyncService unconfigured(*f.sd, SdLayout{}, f.nas, "", f.bus, f.system, std::make_unique<ImmediateTaskRunner>());
    EXPECT_FALSE(unconfigured.isConfigured());
    CancellationToken cancel;
    EXPECT_EQ(unconfigured.syncNow(cancel).error().code, ErrorCode::NotConfigured);
}
