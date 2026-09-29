// HttpClient against a real local HTTP server (tools/test_ftp_server.py
// --http-port-file): index, streamed downloads, Range resume, auth, errors.
//
// Skipped unless RM_TEST_HTTP_PORT is set.

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdlib>
#include <optional>

#include "MockSdCard.hpp"
#include "TempDir.hpp"
#include "retromanager/core/Crc32.hpp"
#include "retromanager/core/ITaskRunner.hpp"
#include "retromanager/network/HttpClient.hpp"
#include "retromanager/network/SourceFactory.hpp"
#include "retromanager/platform/LocalFileSystem.hpp"
#include "retromanager/services/DownloadQueueManager.hpp"
#include "retromanager/services/ShopService.hpp"
#include "retromanager/services/ThumbnailManager.hpp"

using namespace rm;

namespace {

class Http : public ::testing::Test {
  protected:
    void SetUp() override {
        const char* port = std::getenv("RM_TEST_HTTP_PORT");
        if (port == nullptr || *port == '\0') GTEST_SKIP() << "RM_TEST_HTTP_PORT not set (see tools/test_ftp_server.py)";
        base = std::string("http://127.0.0.1:") + port;
    }

    HttpClient client(const std::string& path = "/shop/index.json", std::string user = "", std::string password = "") {
        HttpConfig config;
        config.indexUrl = base + path;
        config.username = std::move(user);
        config.password = std::move(password);
        config.connectTimeoutSeconds = 5;
        return HttpClient(config);
    }

    Result<std::string> get(HttpClient& http, const std::string& url, std::uint64_t offset = 0,
                            std::vector<TransferProgress>* ticks = nullptr) {
        std::string body;
        CancellationToken cancel;
        Status status = http.downloadFileFrom(
            url, offset, [&](const char* d, std::size_t n) { body.append(d, n); return success(); },
            [&](const TransferProgress& p) {
                if (ticks) ticks->push_back(p);
            },
            cancel);
        if (!status) return status.error();
        return body;
    }

    std::string base;
};

const char* kSmw = "/shop/roms/snes/Super%20Mario%20World%20(USA).sfc";

}  // namespace

TEST_F(Http, LoadsAShopIndexFromTheWeb) {
    HttpClient http = client();
    ImmediateTaskRunner tasks;
    auto index = ShopService(http, tasks).loadIndex();
    ASSERT_TRUE(index.ok()) << index.error().describe();
    EXPECT_EQ(index.value().name, "Test FTP shop");
    ASSERT_FALSE(index.value().games.empty());
    EXPECT_EQ(index.value().games[0].romUrl.rfind(base + "/shop/roms/", 0), 0u);  // relative URLs resolved over HTTP
}

TEST_F(Http, StreamsAFile) {
    HttpClient http = client();
    std::vector<TransferProgress> ticks;
    auto body = get(http, base + kSmw, 0, &ticks);
    ASSERT_TRUE(body.ok()) << body.error().describe();
    EXPECT_EQ(body.value(), "MOCK ROM snes\n");
    ASSERT_FALSE(ticks.empty());
    EXPECT_EQ(ticks.back().total, 14u);
}

TEST_F(Http, ResumesWithARangeRequest) {
    HttpClient http = client();
    std::vector<TransferProgress> ticks;
    auto tail = get(http, base + kSmw, 5, &ticks);
    ASSERT_TRUE(tail.ok()) << tail.error().describe();
    EXPECT_EQ(tail.value(), std::string("MOCK ROM snes\n").substr(5));
    ASSERT_FALSE(ticks.empty());
    EXPECT_EQ(ticks.back().received, 14u);  // whole-file numbers
    EXPECT_EQ(ticks.back().total, 14u);
}

TEST_F(Http, AServerIgnoringRangeIsReportedSoTheCallerStartsOver) {
    HttpClient http = client();
    std::string received;
    auto tail = get(http, base + std::string("/norange") + kSmw, 5);
    EXPECT_EQ(tail.error().code, ErrorCode::Unsupported);  // no wrong bytes delivered
    EXPECT_EQ(get(http, base + kSmw, 1000).error().code, ErrorCode::Unsupported);  // 416: past the end
}

TEST_F(Http, ErrorPagesNeverReachTheSink) {
    HttpClient http = client();
    auto missing = get(http, base + "/shop/nope.sfc");
    EXPECT_EQ(missing.error().code, ErrorCode::NotFound);
    EXPECT_EQ(get(http, base + "/status/500").error().code, ErrorCode::NetworkError);
    EXPECT_EQ(get(http, base + "/status/403").error().code, ErrorCode::PermissionDenied);
}

TEST_F(Http, FollowsRedirects) {
    HttpClient http = client();
    auto body = get(http, base + "/redirect" + kSmw);
    ASSERT_TRUE(body.ok()) << body.error().describe();
    EXPECT_EQ(body.value(), "MOCK ROM snes\n");
}

TEST_F(Http, BasicAuthOnItsOwnServer) {
    HttpClient anonymous = client("/private/shop/index.json");
    EXPECT_EQ(anonymous.fetchIndex().error().code, ErrorCode::AuthenticationFailed);
    HttpClient wrong = client("/private/shop/index.json", "retro", "nope");
    EXPECT_EQ(wrong.fetchIndex().error().code, ErrorCode::AuthenticationFailed);
    HttpClient right = client("/private/shop/index.json", "retro", "manager");
    ASSERT_TRUE(right.fetchIndex().ok()) << right.fetchIndex().error().describe();
}

TEST_F(Http, CredentialsAreNotSentToAnotherServer) {
    // Same server as far as HTTP goes, but another host name: no Authorization header.
    HttpClient right = client("/private/shop/index.json", "retro", "manager");
    std::string other = "http://localhost:" + base.substr(base.rfind(':') + 1) + "/private/shop/index.json";
    EXPECT_EQ(get(right, other).error().code, ErrorCode::AuthenticationFailed);
}

TEST_F(Http, TheQueueResumesAnInterruptedHttpDownload) {
    test::TempDir sd;
    LocalFileSystem fs(sd.path());
    HttpClient http = client();
    ImmediateTaskRunner tasks;
    auto index = ShopService(http, tasks).loadIndex();
    ASSERT_TRUE(index.ok());
    GameEntry big;
    for (const GameEntry& g : index.value().games) {
        if (g.title == "Big Test ROM") big = g;
    }
    ASSERT_FALSE(big.romUrl.empty());
    ASSERT_GT(big.sizeBytes, 1024u);
    const std::uint64_t kept = std::min<std::uint64_t>(5 * 1024 * 1024, big.sizeBytes / 2);
    {
        auto install = RomStore(fs, SdLayout{}).beginInstall(big);
        ASSERT_TRUE(install.ok());
        CancellationToken cancel;
        Status dropped = http.downloadFile(
            big.romUrl,
            [&](const char* d, std::size_t n) -> Status {
                std::size_t take = static_cast<std::size_t>(std::min<std::uint64_t>(n, kept - install.value()->bytesWritten()));
                if (Status w = install.value()->write(d, take); !w) return w;
                if (install.value()->bytesWritten() >= kept) return makeError(ErrorCode::NetworkError, "Wi-Fi lost");
                return success();
            },
            nullptr, cancel);
        ASSERT_EQ(dropped.error().code, ErrorCode::NetworkError);
        ASSERT_TRUE(install.value()->suspend().ok());
    }

    EventBus bus(tasks);
    RomStore store(fs, SdLayout{});
    NullSystem system;
    DownloadQueueManager queue(http, store, bus, system, std::make_unique<ImmediateTaskRunner>());
    std::optional<DownloadFinished> finished;
    std::optional<DownloadProgressed> first;
    auto s1 = bus.subscribe<DownloadFinished>([&](const DownloadFinished& e) { finished = e; });
    auto s2 = bus.subscribe<DownloadProgressed>([&](const DownloadProgressed& e) {
        if (!first) first = e;
    });
    queue.start(big);

    ASSERT_TRUE(finished.has_value());
    ASSERT_TRUE(finished->result.ok()) << finished->result.error().describe();  // CRC of the whole file
    ASSERT_TRUE(first.has_value());
    EXPECT_EQ(first->resumedFrom, kept);
    EXPECT_EQ(fs.stat(finished->destination).value().size, big.sizeBytes);
}

TEST_F(Http, TheScraperFetchesLibretroBoxartsOverHttp) {
    test::TempDir dir;
    LocalFileSystem fs(dir.path());
    ASSERT_TRUE(test::copyHostTree(test::fixtureSdCardDir(), fs).ok());
    HttpClient web = client();
    ThumbnailManager thumbnails(fs, SdLayout{}, web, base + "/thumbnails/");  // a local stand-in for thumbnails.libretro.com
    CancellationToken cancel;

    GameEntry smw;
    smw.system = "snes";
    smw.fileName = "Super Mario World (USA).sfc";  // no boxart in the index
    auto found = thumbnails.run(smw, "/roms/snes/Super Mario World (USA).sfc", cancel);
    ASSERT_TRUE(found.has_value());
    ASSERT_TRUE(found->ok()) << found->error().describe();
    auto png = fs.readFile("/retroarch/thumbnails/Nintendo - Super Nintendo Entertainment System/Named_Boxarts/Super Mario World (USA).png");
    ASSERT_TRUE(png.ok());
    EXPECT_TRUE(ThumbnailManager::validate(png.value()).ok());

    GameEntry tom;  // "&" in the name: "_" on the server, percent-encoded in the URL
    tom.system = "gba";
    tom.fileName = "Tom & Jerry (USA).gba";
    ASSERT_TRUE(thumbnails.run(tom, "/roms/gba/Tom & Jerry (USA).gba", cancel)->ok());
    EXPECT_TRUE(fs.isFile("/retroarch/thumbnails/Nintendo - Game Boy Advance/Named_Boxarts/Tom _ Jerry (USA).png"));

    GameEntry unknown;  // not on the server: silent
    unknown.system = "gba";
    unknown.fileName = "Homebrew Thing.gba";
    EXPECT_FALSE(thumbnails.run(unknown, "/roms/gba/Homebrew Thing.gba", cancel).has_value());
}

TEST_F(Http, RawSpacesInUrlsAreEncoded) {
    ShopConfig shop;
    shop.type = "http";
    shop.url = base + "/shop ds/index.json";
    auto config = httpConfigFromShop(shop, "");
    ASSERT_TRUE(config.ok()) << config.error().describe();
    HttpClient http(config.value());
    ImmediateTaskRunner tasks;
    auto index = ShopService(http, tasks).loadIndex();
    ASSERT_TRUE(index.ok()) << index.error().describe();
    ASSERT_EQ(index.value().games.size(), 1u);
    auto body = get(http, index.value().games[0].romUrl);
    ASSERT_TRUE(body.ok()) << body.error().describe();
    EXPECT_EQ(body.value(), "MOCK ROM nds space\n");
    // Already encoded parts are left alone.
    auto smw = get(http, base + "/shop/roms/snes/Super%20Mario World%20(USA).sfc");
    ASSERT_TRUE(smw.ok()) << smw.error().describe();
}

TEST_F(Http, ErrorsCarryTheCurlCodeOrTheHttpStatus) {
    HttpClient http = client();
    auto missing = get(http, base + "/status/404");
    ASSERT_FALSE(missing.ok());
    EXPECT_NE(missing.error().message.find("HTTP 404"), std::string::npos) << missing.error().message;
    auto refused = get(http, "http://127.0.0.1:1/index.json");
    ASSERT_FALSE(refused.ok());
    EXPECT_NE(refused.error().message.find("cURL error 7"), std::string::npos) << refused.error().message;
}
