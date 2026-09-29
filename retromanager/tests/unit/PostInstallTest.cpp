#include <gtest/gtest.h>

#include "MemoryFileSystem.hpp"
#include "MockSdCard.hpp"
#include "retromanager/network/MockRemoteSource.hpp"
#include "retromanager/parsers/CfgDocument.hpp"
#include "retromanager/services/CheatManager.hpp"
#include "retromanager/services/EmulatorConfigurator.hpp"

using namespace rm;

namespace {

const char* kCfg = "/retroarch/retroarch.cfg";
const std::string kCheatUrl = "ftp://mock.local/shop/cheats/nds/Pokemon%20Platine%20(France).cht";
const std::string kCheat =
    "cheats = 1\n\ncheat0_desc = \"Rare Candy x999\"\ncheat0_code = \"021C0000+03E7\"\ncheat0_enable = false\n";

GameEntry platine() {
    GameEntry game;
    game.id = "nds/Pokemon Platine (France).nds";
    game.title = "Pokémon Platine";
    game.system = "nds";
    game.fileName = "Pokemon Platine (France).nds";
    game.cheatUrl = kCheatUrl;
    return game;
}

const std::string kRomPath = "/roms/nds/Pokemon Platine (France).nds";

}  // namespace

// --- EmulatorConfigurator ------------------------------------------------

TEST(EmulatorConfigurator, PointsTheBrowserAtTheRomFolderAndPreservesTheRest) {
    auto sd = test::makeMockSdCard();
    const std::string before = sd->readFile(kCfg).value();
    EmulatorConfigurator configurator(*sd, SdLayout{});
    CancellationToken cancel;

    auto result = configurator.run(platine(), kRomPath, cancel);
    ASSERT_TRUE(result.has_value());
    ASSERT_TRUE(result->ok()) << result->error().describe();

    const std::string after = sd->readFile(kCfg).value();
    EXPECT_EQ(CfgDocument::parse(after).get("rgui_browser_directory"), "/roms/nds/");

    // Byte-for-byte identical except for that one value.
    std::string expected = before;
    std::size_t at = expected.find("rgui_browser_directory = \"");
    ASSERT_NE(at, std::string::npos) << "fixture should contain the key";
    std::size_t valueBegin = at + std::string("rgui_browser_directory = \"").size();
    expected.replace(valueBegin, expected.find('"', valueBegin) - valueBegin, "/roms/nds/");
    EXPECT_EQ(after, expected);
}

TEST(EmulatorConfigurator, KeepsAPristineBackupOnce) {
    auto sd = test::makeMockSdCard();
    const std::string original = sd->readFile(kCfg).value();
    EmulatorConfigurator configurator(*sd, SdLayout{});

    ASSERT_TRUE(configurator.setBrowserDirectory("/roms/nds/").ok());
    ASSERT_TRUE(configurator.setBrowserDirectory("/roms/gba/").ok());
    EXPECT_EQ(sd->readFile(configurator.backupPath()).value(), original);  // not the intermediate version
    EXPECT_EQ(CfgDocument::parse(sd->readFile(kCfg).value()).get("rgui_browser_directory"), "/roms/gba/");
}

TEST(EmulatorConfigurator, DoesNotRewriteWhenAlreadyConfigured) {
    auto sd = test::makeMockSdCard();
    EmulatorConfigurator configurator(*sd, SdLayout{});
    ASSERT_TRUE(configurator.setBrowserDirectory("/roms/nds/").ok());

    sd->setReadOnly(true);  // any write attempt would now fail
    EXPECT_TRUE(configurator.setBrowserDirectory("/roms/nds/").ok());
}

TEST(EmulatorConfigurator, CreatesTheCfgWhenRetroArchNeverSavedOne) {
    auto sd = test::makeMockSdCard();
    ASSERT_TRUE(sd->remove(kCfg).ok());
    EmulatorConfigurator configurator(*sd, SdLayout{});

    ASSERT_TRUE(configurator.setBrowserDirectory("/roms/nds/").ok());
    CfgDocument created = CfgDocument::parse(sd->readFile(kCfg).value());
    EXPECT_EQ(created.get("rgui_browser_directory"), "/roms/nds/");
    EXPECT_EQ(created.keys().size(), 1u);
    EXPECT_FALSE(sd->exists(configurator.backupPath()));  // nothing to back up
}

TEST(EmulatorConfigurator, SkipsWhenRetroArchIsNotInstalled) {
    test::MemoryFileSystem fs;
    EmulatorConfigurator configurator(fs, SdLayout{});

    Status status = configurator.setBrowserDirectory("/roms/nds/");
    ASSERT_FALSE(status.ok());
    EXPECT_EQ(status.error().code, ErrorCode::NotFound);
    EXPECT_EQ(fs.nodeCount(), 0u);  // no /retroarch created behind the user's back
}

TEST(EmulatorConfigurator, ReportsAWriteProtectedCard) {
    auto sd = test::makeMockSdCard();
    sd->setReadOnly(true);
    EmulatorConfigurator configurator(*sd, SdLayout{});
    Status status = configurator.setBrowserDirectory("/roms/nds/");
    ASSERT_FALSE(status.ok());
    EXPECT_EQ(status.error().code, ErrorCode::PermissionDenied);
}

// --- CheatManager --------------------------------------------------------

TEST(CheatManager, InstallsTheCheatInTheLibretroFolder) {
    auto sd = test::makeMockSdCard();
    MockRemoteSource source("{}", "");
    source.addFile(kCheatUrl, kCheat);
    CheatManager cheats(*sd, SdLayout{}, source);
    CancellationToken cancel;

    auto result = cheats.run(platine(), kRomPath, cancel);
    ASSERT_TRUE(result.has_value());
    ASSERT_TRUE(result->ok()) << result->error().describe();
    EXPECT_EQ(sd->readFile("/retroarch/cheats/Nintendo - Nintendo DS/Pokemon Platine (France).cht").value(), kCheat);
}

TEST(CheatManager, NamesTheCheatAfterTheInstalledRom) {
    test::MemoryFileSystem fs;
    MockRemoteSource source("{}", "");
    CheatManager cheats(fs, SdLayout{}, source);
    // The ROM name on the card may differ from the index (sanitized).
    EXPECT_EQ(cheats.destinationFor(platine(), "/roms/nds/Zelda_ Spirit Tracks.nds").value(),
              "/retroarch/cheats/Nintendo - Nintendo DS/Zelda_ Spirit Tracks.cht");
}

TEST(CheatManager, HonorsRetroArchCheatDatabasePath) {
    auto sd = test::makeMockSdCard();
    CfgDocument cfg = CfgDocument::parse(sd->readFile(kCfg).value());
    ASSERT_TRUE(cfg.set("cheat_database_path", "/custom/cheats").ok());
    ASSERT_TRUE(sd->writeFile(kCfg, cfg.serialize()).ok());

    MockRemoteSource source("{}", "");
    CheatManager cheats(*sd, SdLayout{}, source);
    EXPECT_EQ(cheats.cheatsDirectory(), "/custom/cheats");

    // Relative or RetroArch-special values (":/cheats") fall back to the default.
    ASSERT_TRUE(cfg.set("cheat_database_path", ":/cheats").ok());
    ASSERT_TRUE(sd->writeFile(kCfg, cfg.serialize()).ok());
    EXPECT_EQ(cheats.cheatsDirectory(), "/retroarch/cheats");
}

TEST(CheatManager, DoesNothingWithoutCheatUrl) {
    auto sd = test::makeMockSdCard();
    MockRemoteSource source("{}", "");
    CheatManager cheats(*sd, SdLayout{}, source);
    GameEntry game = platine();
    game.cheatUrl.clear();
    CancellationToken cancel;
    EXPECT_FALSE(cheats.run(game, kRomPath, cancel).has_value());
    EXPECT_EQ(source.downloadCount(), 0);
}

TEST(CheatManager, RejectsSomethingThatIsNotACheatFile) {
    auto sd = test::makeMockSdCard();
    MockRemoteSource source("{}", "");
    source.addFile(kCheatUrl, "<html><body>404 Not Found</body></html>");
    CheatManager cheats(*sd, SdLayout{}, source);
    CancellationToken cancel;

    auto result = cheats.run(platine(), kRomPath, cancel);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->error().code, ErrorCode::IntegrityError);
    EXPECT_FALSE(sd->exists("/retroarch/cheats/Nintendo - Nintendo DS"));
}

TEST(CheatManager, ValidatesCheatFiles) {
    EXPECT_TRUE(CheatManager::validate(kCheat).ok());
    EXPECT_TRUE(CheatManager::validate("cheats = 0\n").ok());
    EXPECT_FALSE(CheatManager::validate("").ok());
    EXPECT_FALSE(CheatManager::validate("cheats = many\n").ok());
    EXPECT_FALSE(CheatManager::validate("cheats = -1\n").ok());
    EXPECT_FALSE(CheatManager::validate("cheat0_desc = \"orphan\"\n").ok());
}

TEST(CheatManager, CapsTheDownloadSize) {
    auto sd = test::makeMockSdCard();
    MockRemoteSource source("{}", "");
    source.addSyntheticFile(kCheatUrl, CheatManager::kMaxCheatBytes + 1);
    CheatManager cheats(*sd, SdLayout{}, source);
    CancellationToken cancel;
    auto result = cheats.run(platine(), kRomPath, cancel);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->error().code, ErrorCode::IoError);
}

TEST(CheatManager, PropagatesSourceErrorsAndUnknownSystems) {
    auto sd = test::makeMockSdCard();
    MockRemoteSource source("{}", "");  // no file registered
    CheatManager cheats(*sd, SdLayout{}, source);
    CancellationToken cancel;
    EXPECT_EQ(cheats.run(platine(), kRomPath, cancel)->error().code, ErrorCode::NotFound);

    GameEntry arcade = platine();
    arcade.system = "arcade";  // no single libretro cheat folder
    EXPECT_EQ(cheats.run(arcade, "/roms/arcade/x.zip", cancel)->error().code, ErrorCode::Unsupported);
}

TEST(CheatManager, SkipsWhenRetroArchIsNotInstalled) {
    test::MemoryFileSystem fs;
    MockRemoteSource source("{}", "");
    source.addFile(kCheatUrl, kCheat);
    CheatManager cheats(fs, SdLayout{}, source);
    CancellationToken cancel;
    EXPECT_EQ(cheats.run(platine(), kRomPath, cancel)->error().code, ErrorCode::NotFound);
    EXPECT_EQ(source.downloadCount(), 0);
}

// --- the whole scenario on the demo shop -------------------------------------

#include "retromanager/services/DownloadQueueManager.hpp"
#include "retromanager/services/ShopService.hpp"

TEST(PostInstallScenario, DsDownloadConfiguresRetroArchAndInstallsCheats) {
    auto sd = test::makeMockSdCard();
    MockRemoteSource demo;  // the demo shop, with its cheat files
    ImmediateTaskRunner tasks;
    auto index = ShopService(demo, tasks).loadIndex();
    ASSERT_TRUE(index.ok());
    GameEntry platineEntry;
    for (const GameEntry& g : index.value().games) {
        if (g.title == "Pokémon Platine") platineEntry = g;
    }
    ASSERT_FALSE(platineEntry.cheatUrl.empty());

    SdLayout layout;
    RomStore store(*sd, layout);
    EventBus bus(tasks);
    NullSystem system;
    EmulatorConfigurator configurator(*sd, layout);
    CheatManager cheats(*sd, layout, demo);
    DownloadQueueManager downloads(demo, store, bus, system, std::make_unique<ImmediateTaskRunner>());
    downloads.addPostInstallStep(configurator);
    downloads.addPostInstallStep(cheats);
    std::optional<DownloadFinished> finished;
    auto subscription = bus.subscribe<DownloadFinished>([&](const DownloadFinished& e) { finished = e; });

    downloads.start(platineEntry);

    ASSERT_TRUE(finished.has_value());
    ASSERT_TRUE(finished->result.ok()) << finished->result.error().describe();
    ASSERT_EQ(finished->steps.size(), 2u);
    EXPECT_TRUE(finished->steps[0].result.ok()) << finished->steps[0].result.error().describe();
    EXPECT_TRUE(finished->steps[1].result.ok()) << finished->steps[1].result.error().describe();

    EXPECT_EQ(sd->stat("/roms/nds/Pokemon Platine (France).nds").value().size, 134217728u);
    EXPECT_EQ(CfgDocument::parse(sd->readFile(kCfg).value()).get("rgui_browser_directory"), "/roms/nds/");
    auto cht = sd->readFile("/retroarch/cheats/Nintendo - Nintendo DS/Pokemon Platine (France).cht");
    ASSERT_TRUE(cht.ok());
    EXPECT_EQ(CfgDocument::parse(cht.value()).get("cheats"), "2");
}

// --- SysClkConfigurator ------------------------------------------------------

#include "retromanager/parsers/IniDocument.hpp"
#include "retromanager/services/SysClkConfigurator.hpp"

namespace {

const char* kSysClkIni = "/config/sys-clk/config.ini";

GameEntry gameFor(const std::string& system) {
    GameEntry game;
    game.id = system + "/Game";
    game.system = system;
    game.fileName = "Game";
    return game;
}

}  // namespace

TEST(SysClkConfigurator, OnlyDemandingSystemsAreBoosted) {
    auto sd = test::makeMockSdCard();
    SysClkConfigurator sysclk(*sd, SdLayout{});
    CancellationToken cancel;
    EXPECT_FALSE(sysclk.run(gameFor("snes"), "/roms/snes/Game", cancel).has_value());
    EXPECT_FALSE(sysclk.run(gameFor("nds"), "/roms/nds/Game", cancel).has_value());
    // 3DS runs in Citra (standalone), not RetroArch: never a RetroArch profile.
    EXPECT_FALSE(sysclk.run(gameFor("3ds"), "/roms/3ds/Game", cancel).has_value());
    for (const char* system : {"n64", "psx"}) {
        auto result = sysclk.run(gameFor(system), "/roms/x/Game", cancel);
        ASSERT_TRUE(result.has_value()) << system;
        EXPECT_TRUE(result->ok()) << system;
    }
}

TEST(SysClkConfigurator, DefaultsToTheAlbumAppletThatRunsHbmenuHomebrews) {
    EXPECT_STREQ(SysClkConfigurator::kDefaultTitleId, "010000000000100D");
}

TEST(SysClkConfigurator, RaisesTheAlbumProfileAndKeepsTheRest) {
    auto sd = test::makeMockSdCard();
    const std::string before = sd->readFile(kSysClkIni).value();
    SysClkConfigurator sysclk(*sd, SdLayout{});

    ASSERT_TRUE(sysclk.applyMaxCpuProfile().ok());

    const std::string after = sd->readFile(kSysClkIni).value();
    // Only the Album section changes: its value in place, the missing key after it.
    EXPECT_EQ(after,
              "; Mock sys-clk configuration\n"
              "[values]\n"
              "temp_log_interval_ms=0\n"
              "freq_log_interval_ms=0\n"
              "\n"
              "; Album applet (homebrew launched from hbmenu): mild handheld boost\n"
              "[010000000000100D]\n"
              "handheld_cpu=1785\n"
              "docked_cpu=1785\n"
              "\n"
              "; Mario Kart 8 Deluxe\n"
              "[0100152000022000]\n"
              "docked_gpu=921\n");
    EXPECT_EQ(sd->readFile(sysclk.backupPath()).value(), before);
}

TEST(SysClkConfigurator, AddsAMissingProfileAtTheEnd) {
    auto sd = test::makeMockSdCard();
    const std::string before = sd->readFile(kSysClkIni).value();
    SysClkConfigurator sysclk(*sd, SdLayout{}, "05B9D58000000000");

    ASSERT_TRUE(sysclk.applyMaxCpuProfile().ok());

    const std::string after = sd->readFile(kSysClkIni).value();
    EXPECT_EQ(after.substr(0, before.size()), before);  // existing content untouched, profile appended
    IniDocument ini = IniDocument::parse(after);
    EXPECT_EQ(ini.get("05B9D58000000000", "handheld_cpu"), "1785");
    EXPECT_EQ(ini.get("05B9D58000000000", "docked_cpu"), "1785");
}

TEST(SysClkConfigurator, RaisesAnExistingProfileWithoutTouchingItsOtherKeys) {
    auto sd = test::makeMockSdCard();
    ASSERT_TRUE(sd->writeFile(kSysClkIni, "[05b9d58000000000]\nhandheld_cpu=1020\nhandheld_gpu=307\n").ok());
    SysClkConfigurator sysclk(*sd, SdLayout{}, "05B9D58000000000");  // title id case differs from the file

    ASSERT_TRUE(sysclk.applyMaxCpuProfile().ok());
    EXPECT_EQ(sd->readFile(kSysClkIni).value(),
              "[05b9d58000000000]\nhandheld_cpu=1785\nhandheld_gpu=307\ndocked_cpu=1785\n");
}

TEST(SysClkConfigurator, IsIdempotent) {
    auto sd = test::makeMockSdCard();
    SysClkConfigurator sysclk(*sd, SdLayout{});
    ASSERT_TRUE(sysclk.applyMaxCpuProfile().ok());
    sd->setReadOnly(true);  // a second run must not write anything
    EXPECT_TRUE(sysclk.applyMaxCpuProfile().ok());
}

TEST(SysClkConfigurator, CreatesConfigIniWhenSysClkHasNone) {
    auto sd = test::makeMockSdCard();
    ASSERT_TRUE(sd->remove(kSysClkIni).ok());
    SysClkConfigurator sysclk(*sd, SdLayout{}, "0100000000001000");

    ASSERT_TRUE(sysclk.applyMaxCpuProfile().ok());
    EXPECT_EQ(sd->readFile(kSysClkIni).value(), "[0100000000001000]\nhandheld_cpu=1785\ndocked_cpu=1785\n");
    EXPECT_FALSE(sd->exists(sysclk.backupPath()));
}

TEST(SysClkConfigurator, SkipsWhenSysClkIsNotInstalled) {
    test::MemoryFileSystem fs;
    SysClkConfigurator sysclk(fs, SdLayout{});
    Status status = sysclk.applyMaxCpuProfile();
    EXPECT_EQ(status.error().code, ErrorCode::NotFound);
    EXPECT_EQ(fs.nodeCount(), 0u);
}

TEST(SysClkConfigurator, ValidatesTitleIds) {
    EXPECT_TRUE(SysClkConfigurator::isValidTitleId("05B9D58000000000"));
    EXPECT_TRUE(SysClkConfigurator::isValidTitleId("010000000000100d"));
    EXPECT_FALSE(SysClkConfigurator::isValidTitleId("05B9D580"));
    EXPECT_FALSE(SysClkConfigurator::isValidTitleId("05B9D5800000000G"));
    EXPECT_FALSE(SysClkConfigurator::isValidTitleId(""));
}
