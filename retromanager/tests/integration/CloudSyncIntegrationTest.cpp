// Cloud saves over a real FTP server, between two "consoles": two SD
// cards on disk (LocalFileSystem) sharing one NAS folder.
// Skipped unless RM_TEST_FTP_PORT is set.

#include <gtest/gtest.h>

#include <chrono>
#include <cstdlib>
#include <thread>

#include "MockSdCard.hpp"
#include "TempDir.hpp"
#include "retromanager/network/FtpClient.hpp"
#include "retromanager/platform/LocalFileSystem.hpp"
#include "retromanager/services/CloudSyncService.hpp"

using namespace rm;

namespace {

struct Console {
    test::TempDir dir;
    LocalFileSystem sd{dir.path()};
    ImmediateTaskRunner tasks;
    EventBus bus{tasks};
    NullSystem system;
    std::unique_ptr<FtpClient> ftp;
    std::unique_ptr<CloudSyncService> sync;

    Console(const FtpConfig& config, const std::string& nasFolder) {
        EXPECT_TRUE(test::copyHostTree(test::fixtureSdCardDir(), sd).ok());
        ftp = std::make_unique<FtpClient>(config);
        sync = std::make_unique<CloudSyncService>(sd, SdLayout{}, *ftp, nasFolder, bus, system,
                                                  std::make_unique<ImmediateTaskRunner>());
    }

    SyncReport run() {
        CancellationToken cancel;
        auto report = sync->syncNow(cancel);
        EXPECT_TRUE(report.ok()) << (report.ok() ? "" : report.error().describe());
        return report.ok() ? report.value() : SyncReport{};
    }

    std::string save(const std::string& name) { return sd.readFile("/retroarch/saves/" + name).valueOr("<missing>"); }
};

class CloudSyncOverFtp : public ::testing::Test {
  protected:
    void SetUp() override {
        const char* port = std::getenv("RM_TEST_FTP_PORT");
        if (port == nullptr || *port == '\0') GTEST_SKIP() << "RM_TEST_FTP_PORT not set (see tools/test_ftp_server.py)";
        config.host = "127.0.0.1";
        config.port = static_cast<std::uint16_t>(std::atoi(port));
        config.username = "retro";
        config.password = "manager";
        nasFolder = "ftp://127.0.0.1:" + std::string(port) + "/saves/sync-" +
                    std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "/";
    }
    FtpConfig config;
    std::string nasFolder;
};

}  // namespace

TEST_F(CloudSyncOverFtp, TwoConsolesShareTheirSavesWithoutPingPong) {
    Console a(config, nasFolder);
    Console b(config, nasFolder);
    ASSERT_TRUE(b.sd.remove("/retroarch/saves/Super Mario World (USA).srm").ok());  // B never played it

    // A publishes its save, B receives it.
    EXPECT_EQ(a.run().uploaded, 1);
    SyncReport firstB = b.run();
    EXPECT_EQ(firstB.downloaded, 1);
    EXPECT_EQ(b.save("Super Mario World (USA).srm"), "MOCK SAVE\n");

    // Nothing changed: nobody transfers anything, however often they sync.
    for (int i = 0; i < 2; ++i) {
        SyncReport ra = a.run(), rb = b.run();
        EXPECT_EQ(ra.uploaded + ra.downloaded + ra.conflicts, 0);
        EXPECT_EQ(rb.uploaded + rb.downloaded + rb.conflicts, 0);
    }

    // B plays (a new save, in a core subfolder too), A gets it.
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));  // a distinct second for the mtime
    ASSERT_TRUE(b.sd.writeFile("/retroarch/saves/Super Mario World (USA).srm", "WORLD 2").ok());
    ASSERT_TRUE(b.sd.createDirectories("/retroarch/saves/melonDS").ok());
    ASSERT_TRUE(b.sd.writeFile("/retroarch/saves/melonDS/Pokemon Platine (France).sav", "BADGE 3").ok());
    EXPECT_EQ(b.run().uploaded, 2);
    SyncReport ra = a.run();
    EXPECT_EQ(ra.downloaded, 2);
    EXPECT_EQ(a.save("Super Mario World (USA).srm"), "WORLD 2");
    EXPECT_EQ(a.save("melonDS/Pokemon Platine (France).sav"), "BADGE 3");
    EXPECT_EQ(a.run().downloaded + a.run().uploaded, 0);
}

TEST_F(CloudSyncOverFtp, SimultaneousPlayKeepsBothVersions) {
    Console a(config, nasFolder);
    Console b(config, nasFolder);
    a.run();
    b.run();

    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    ASSERT_TRUE(a.sd.writeFile("/retroarch/saves/Super Mario World (USA).srm", "A PLAYED").ok());
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    ASSERT_TRUE(b.sd.writeFile("/retroarch/saves/Super Mario World (USA).srm", "B PLAYED LATER").ok());

    EXPECT_EQ(a.run().uploaded, 1);    // A syncs first: the NAS takes A's version
    SyncReport rb = b.run();           // B also changed it since the last sync: conflict
    EXPECT_EQ(rb.conflicts, 1);
    EXPECT_EQ(b.save("Super Mario World (USA).srm"), "B PLAYED LATER");  // newest kept...
    ASSERT_EQ(rb.conflictCopies.size(), 1u);
    EXPECT_EQ(b.sd.readFile(rb.conflictCopies[0]).value(), "A PLAYED");  // ...A's progress not lost
    EXPECT_EQ(a.run().downloaded, 1);
    EXPECT_EQ(a.save("Super Mario World (USA).srm"), "B PLAYED LATER");
}
