// Streaming downloads against a real FTP server (tools/test_ftp_server.py):
// FtpClient -> DownloadService -> RomStore -> LocalFileSystem on a temp dir.
//
// Skipped unless RM_TEST_FTP_PORT is set (and RM_TEST_FTPS_PORT for FTPS).

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <sstream>
#include <vector>

#include "MockSdCard.hpp"
#include "TempDir.hpp"
#include "retromanager/core/Crc32.hpp"
#include "retromanager/core/ITaskRunner.hpp"
#include "retromanager/network/FtpClient.hpp"
#include "retromanager/platform/LocalFileSystem.hpp"
#include "retromanager/parsers/CfgDocument.hpp"
#include "retromanager/parsers/PlaylistDocument.hpp"
#include "retromanager/services/BiosManager.hpp"
#include "retromanager/services/CheatManager.hpp"
#include "retromanager/services/DownloadService.hpp"
#include "retromanager/services/EmulatorConfigurator.hpp"
#include "retromanager/services/PlaylistManager.hpp"
#include "retromanager/services/ShopService.hpp"
#include "retromanager/services/ThumbnailManager.hpp"

using namespace rm;

namespace {

std::optional<std::uint16_t> portFromEnv(const char* name) {
    const char* value = std::getenv(name);
    if (value == nullptr || *value == '\0') return std::nullopt;
    return static_cast<std::uint16_t>(std::atoi(value));
}

FtpConfig ftpConfig(std::uint16_t port) {
    FtpConfig config;
    config.host = "127.0.0.1";
    config.port = port;
    config.username = "retro";
    config.password = "manager";
    config.indexPath = "/shop/index.json";
    config.connectTimeoutSeconds = 5;
    return config;
}

// Resident memory of this process, in bytes (Linux).
std::optional<std::uint64_t> residentBytes() {
    std::ifstream status("/proc/self/status");
    std::string line;
    while (std::getline(status, line)) {
        if (line.rfind("VmRSS:", 0) == 0) {
            std::istringstream fields(line.substr(6));
            std::uint64_t kib = 0;
            fields >> kib;
            return kib * 1024;
        }
    }
    return std::nullopt;
}

class FtpDownload : public ::testing::Test {
  protected:
    void SetUp() override {
        auto port = portFromEnv("RM_TEST_FTP_PORT");
        if (!port) GTEST_SKIP() << "RM_TEST_FTP_PORT not set (see tools/test_ftp_server.py)";
        client = std::make_unique<FtpClient>(ftpConfig(*port));
        ImmediateTaskRunner tasks;
        auto index = ShopService(*client, tasks).loadIndex();
        ASSERT_TRUE(index.ok()) << index.error().describe();
        for (const GameEntry& game : index.value().games) {
            if (game.title == "Big Test ROM") big = game;
            if (game.title == "Super Mario World") small = game;
            if (game.title == "Test DS Game") ds = game;
        }
        ASSERT_FALSE(big.romUrl.empty()) << "server started with --big-mb 0?";
        ASSERT_FALSE(small.romUrl.empty());
    }

    std::unique_ptr<FtpClient> client;
    GameEntry big;
    GameEntry small;
    GameEntry ds;
    test::TempDir sd;
};

// Runs one download synchronously and collects its events.
struct DownloadRun {
    std::vector<DownloadProgressed> progress;
    std::optional<DownloadFinished> finished;
};

DownloadRun download(IRemoteSource& source, IFileSystem& fs, const GameEntry& game,
             std::function<void(DownloadService&, const DownloadProgressed&)> onProgress = nullptr) {
    ImmediateTaskRunner mainThread;
    EventBus bus(mainThread);
    RomStore store(fs, SdLayout{});
    NullSystem system;
    DownloadService downloads(source, store, bus, system, std::make_unique<ImmediateTaskRunner>());
    DownloadRun run;
    auto p = bus.subscribe<DownloadProgressed>([&](const DownloadProgressed& e) {
        run.progress.push_back(e);
        if (onProgress) onProgress(downloads, e);
    });
    auto f = bus.subscribe<DownloadFinished>([&](const DownloadFinished& e) { run.finished = e; });
    downloads.start(game);
    return run;
}

std::string Crc32Hex(std::string_view data) {
    Crc32 crc;
    crc.update(data.data(), data.size());
    return crc.hex();
}

}  // namespace

TEST_F(FtpDownload, StreamsASmallRomToTheCard) {
    LocalFileSystem fs(sd.path());
    DownloadRun run = download(*client, fs, small);

    ASSERT_TRUE(run.finished.has_value());
    ASSERT_TRUE(run.finished->result.ok()) << run.finished->result.error().describe();
    EXPECT_EQ(run.finished->destination, "/roms/snes/Super Mario World (USA).sfc");
    EXPECT_EQ(fs.readFile("/roms/snes/Super Mario World (USA).sfc").value(), "MOCK ROM snes\n");
}

TEST_F(FtpDownload, StreamsTheBigRomWithCrcCheckAndBoundedMemory) {
    LocalFileSystem fs(sd.path());
    std::optional<std::uint64_t> baseline = residentBytes();
    std::uint64_t peak = baseline.value_or(0);

    DownloadRun run = download(*client, fs, big, [&](DownloadService&, const DownloadProgressed&) {
        if (auto rss = residentBytes()) peak = std::max(peak, *rss);
    });

    ASSERT_TRUE(run.finished.has_value());
    ASSERT_TRUE(run.finished->result.ok()) << run.finished->result.error().describe();  // includes the CRC32 check
    auto info = fs.stat(run.finished->destination);
    ASSERT_TRUE(info.ok());
    EXPECT_EQ(info.value().size, big.sizeBytes);
    EXPECT_EQ(fs.listDirectory("/roms/gba").value().size(), 1u);  // no staging file left
    ASSERT_GE(run.progress.size(), 2u);
    EXPECT_EQ(run.progress.back().received, big.sizeBytes);
    EXPECT_EQ(run.progress.back().total, big.sizeBytes);

#ifdef __linux__
    ASSERT_TRUE(baseline.has_value());
    // A 64 MiB ROM must not grow the process by anything close to its size:
    // only the 1 MiB write buffer and curl's receive buffer are allocated.
    EXPECT_LT(peak - *baseline, 16u * 1024 * 1024)
        << "resident memory grew by " << (peak - *baseline) / 1024 << " KiB while streaming "
        << big.sizeBytes / 1024 << " KiB";
#endif
}

TEST_F(FtpDownload, CancelMidTransferLeavesNothingBehind) {
    LocalFileSystem fs(sd.path());
    DownloadRun run = download(*client, fs, big, [](DownloadService& downloads, const DownloadProgressed& e) {
        if (e.received > 0) downloads.cancel(e.id);  // B pressed during the transfer
    });

    ASSERT_TRUE(run.finished.has_value());
    EXPECT_EQ(run.finished->result.error().code, ErrorCode::Cancelled) << run.finished->result.error().describe();
    EXPECT_FALSE(fs.exists(run.finished->destination));
    EXPECT_TRUE(fs.listDirectory("/roms/gba").value().empty());
    EXPECT_FALSE(std::filesystem::exists(sd.path() / "roms/gba/.Big Test ROM (Synthetic).gba.tmp"));
}

TEST_F(FtpDownload, MissingRemoteFileIsNotFoundAndCleansUp) {
    LocalFileSystem fs(sd.path());
    GameEntry ghost = small;
    ghost.romUrl = "ftp://127.0.0.1:" + std::to_string(ftpConfig(*portFromEnv("RM_TEST_FTP_PORT")).port) +
                   "/shop/roms/snes/Ghost.sfc";
    ghost.fileName = "Ghost.sfc";
    DownloadRun run = download(*client, fs, ghost);

    ASSERT_TRUE(run.finished.has_value());
    EXPECT_EQ(run.finished->result.error().code, ErrorCode::NotFound) << run.finished->result.error().describe();
    EXPECT_TRUE(fs.listDirectory("/roms/snes").value().empty());
}

TEST_F(FtpDownload, NeverSendsCredentialsToAnotherServer) {
    CancellationToken cancel;
    auto sink = [](const char*, std::size_t) { return success(); };
    std::uint16_t port = *portFromEnv("RM_TEST_FTP_PORT");

    EXPECT_EQ(client->downloadFile("ftp://127.0.0.2:" + std::to_string(port) + "/shop/x", sink, nullptr, cancel).error().code,
              ErrorCode::PermissionDenied);
    EXPECT_EQ(client->downloadFile("ftp://127.0.0.1:" + std::to_string(port + 1) + "/shop/x", sink, nullptr, cancel).error().code,
              ErrorCode::PermissionDenied);
    EXPECT_EQ(client->downloadFile("https://127.0.0.1/shop/x", sink, nullptr, cancel).error().code,
              ErrorCode::PermissionDenied);
}

TEST_F(FtpDownload, SinkErrorAbortsTheTransfer) {
    CancellationToken cancel;
    std::uint64_t accepted = 0;
    Status status = client->downloadFile(
        big.romUrl,
        [&](const char*, std::size_t n) {
            accepted += n;
            return accepted > 1024 * 1024 ? Status(makeError(ErrorCode::IoError, "SD card removed")) : success();
        },
        nullptr, cancel);
    ASSERT_FALSE(status.ok());
    EXPECT_EQ(status.error().code, ErrorCode::IoError);
    EXPECT_EQ(status.error().message, "SD card removed");
    EXPECT_LT(accepted, big.sizeBytes);
}

TEST_F(FtpDownload, DsGameEndToEndIntegratesWithRetroArch) {
    // A real SD card layout on disk, then the full pipeline over real FTP.
    LocalFileSystem fs(sd.path());
    ASSERT_TRUE(test::copyHostTree(test::fixtureSdCardDir(), fs).ok());
    ASSERT_FALSE(ds.cheatUrl.empty());

    SdLayout layout;
    ImmediateTaskRunner mainThread;
    EventBus bus(mainThread);
    RomStore store(fs, layout);
    NullSystem system;
    EmulatorConfigurator configurator(fs, layout);
    PlaylistManager playlists(fs, layout);
    ThumbnailManager thumbnails(fs, layout, *client);
    CheatManager cheats(fs, layout, *client);
    DownloadService downloads(*client, store, bus, system, std::make_unique<ImmediateTaskRunner>());
    downloads.addPostInstallStep(configurator);
    downloads.addPostInstallStep(playlists);
    downloads.addPostInstallStep(thumbnails);
    downloads.addPostInstallStep(cheats);
    std::optional<DownloadFinished> finished;
    auto subscription = bus.subscribe<DownloadFinished>([&](const DownloadFinished& e) { finished = e; });

    downloads.start(ds);

    ASSERT_TRUE(finished.has_value());
    ASSERT_TRUE(finished->result.ok()) << finished->result.error().describe();
    ASSERT_EQ(finished->steps.size(), 4u);
    for (const StepOutcome& step : finished->steps) EXPECT_TRUE(step.result.ok()) << step.id << ": " << step.result.error().describe();

    EXPECT_EQ(fs.readFile("/roms/nds/Test DS Game (Europe).nds").value(), "MOCK ROM nds\n");
    EXPECT_EQ(CfgDocument::parse(fs.readFile("/retroarch/retroarch.cfg").value()).get("rgui_browser_directory"), "/roms/nds/");
    EXPECT_TRUE(fs.isFile("/retroarch/retroarch.cfg.rmbak"));
    auto cht = fs.readFile("/retroarch/cheats/Nintendo - Nintendo DS/Test DS Game (Europe).cht");
    ASSERT_TRUE(cht.ok()) << cht.error().describe();
    EXPECT_EQ(CfgDocument::parse(cht.value()).get("cheat1_desc"), "Max Money");

    auto lpl = fs.readFile("/retroarch/playlists/Nintendo - Nintendo DS.lpl");
    ASSERT_TRUE(lpl.ok()) << lpl.error().describe();
    auto items = PlaylistDocument::parse(lpl.value()).value().items();
    ASSERT_EQ(items.size(), 1u);
    EXPECT_EQ(items[0].path, "/roms/nds/Test DS Game (Europe).nds");
    EXPECT_EQ(items[0].label, "Test DS Game (Europe)");
    EXPECT_EQ(items[0].crc32, PlaylistDocument::crcField(Crc32Hex("MOCK ROM nds\n")));

    auto png = fs.readFile("/retroarch/thumbnails/Nintendo - Nintendo DS/Named_Boxarts/Test DS Game (Europe).png");
    ASSERT_TRUE(png.ok()) << png.error().describe();
    EXPECT_TRUE(ThumbnailManager::validate(png.value()).ok());
    // No staging file anywhere.
    for (auto& entry : std::filesystem::recursive_directory_iterator(sd.path())) {
        EXPECT_FALSE(isStagingName(entry.path().filename().string())) << entry.path();
    }
}

TEST_F(FtpDownload, BiosFilesOfferedByTheShopAreCheckedAndInstalled) {
    LocalFileSystem fs(sd.path());
    ASSERT_TRUE(test::copyHostTree(test::fixtureSdCardDir(), fs).ok());
    ImmediateTaskRunner tasks;
    auto index = ShopService(*client, tasks).loadIndex();
    ASSERT_TRUE(index.ok()) << index.error().describe();
    ASSERT_EQ(index.value().bios.size(), 3u);

    EventBus bus(tasks);
    BiosManager bios(fs, SdLayout{}, *client, bus, std::make_unique<ImmediateTaskRunner>());
    auto before = bios.check(index.value().bios);
    auto rowFor = [](const std::vector<BiosStatus>& rows, const std::string& name) {
        return *std::find_if(rows.begin(), rows.end(), [&](const BiosStatus& r) { return r.fileName == name; });
    };
    EXPECT_EQ(rowFor(before, "scph5501.bin").state, BiosState::Missing);
    ASSERT_TRUE(rowFor(before, "scph5501.bin").offer.has_value());

    std::vector<BiosInstalled> installed;
    auto sub = bus.subscribe<BiosInstalled>([&](const BiosInstalled& e) { installed.push_back(e); });
    ASSERT_TRUE(bios.startInstall({*rowFor(before, "scph5501.bin").offer, *rowFor(before, "scph5502.bin").offer}));
    ASSERT_EQ(installed.size(), 2u);
    for (const BiosInstalled& e : installed) EXPECT_TRUE(e.result.ok()) << e.fileName << ": " << e.result.error().describe();

    // Fixture files are fakes: present, but not the dumps the catalogue knows.
    auto after = bios.check(index.value().bios);
    EXPECT_EQ(rowFor(after, "scph5501.bin").state, BiosState::Unrecognized);
    EXPECT_EQ(fs.readFile("/retroarch/system/scph5501.bin").value(), "FAKE PS1 BIOS (USA) - test fixture\n");

    // An MD5 that does not match what the server sends: nothing installed.
    BiosEntry wrong = *rowFor(before, "scph5502.bin").offer;
    wrong.fileName = "scph5500.bin";
    wrong.md5 = "00000000000000000000000000000000";
    CancellationToken cancel;
    EXPECT_EQ(bios.installNow(wrong, cancel).error().code, ErrorCode::IntegrityError);
    EXPECT_FALSE(fs.exists("/retroarch/system/scph5500.bin"));
    for (auto& entry : std::filesystem::recursive_directory_iterator(sd.path())) {
        EXPECT_FALSE(isStagingName(entry.path().filename().string())) << entry.path();
    }
}

// --- FTPS with a self-signed certificate ----------------------------------

class Ftps : public ::testing::Test {
  protected:
    void SetUp() override {
        auto port = portFromEnv("RM_TEST_FTPS_PORT");
        if (!port) GTEST_SKIP() << "RM_TEST_FTPS_PORT not set (see tools/test_ftp_server.py --ftps-port-file)";
        config = ftpConfig(*port);
        config.useTls = true;
    }
    FtpConfig config;
};

TEST_F(Ftps, SelfSignedCertificateIsAcceptedWhenVerificationIsOff) {
    config.verifyPeer = false;
    FtpClient client(config);
    auto index = client.fetchIndex();
    ASSERT_TRUE(index.ok()) << index.error().describe();
    EXPECT_NE(index.value().find("Test FTP shop"), std::string::npos);

    std::string rom;
    CancellationToken cancel;
    ASSERT_TRUE(client
                    .downloadFile(
                        "ftp://127.0.0.1:" + std::to_string(config.port) +
                            "/shop/roms/snes/Super%20Mario%20World%20(USA).sfc",
                        [&](const char* d, std::size_t n) {
                            rom.append(d, n);
                            return success();
                        },
                        nullptr, cancel)
                    .ok());
    EXPECT_EQ(rom, "MOCK ROM snes\n");
}

TEST_F(Ftps, SelfSignedCertificateIsRejectedWhenVerificationIsOn) {
    config.verifyPeer = true;
    auto index = FtpClient(config).fetchIndex();
    ASSERT_FALSE(index.ok());
    EXPECT_EQ(index.error().code, ErrorCode::NetworkError) << index.error().describe();
}

TEST_F(Ftps, PlainFtpIsRefusedByATlsOnlyServer) {
    config.useTls = false;
    auto index = FtpClient(config).fetchIndex();
    EXPECT_FALSE(index.ok());
}
