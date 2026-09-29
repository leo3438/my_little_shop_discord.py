#include <gtest/gtest.h>

#include "MemoryFileSystem.hpp"
#include "MockSdCard.hpp"
#include "retromanager/network/MockRemoteSource.hpp"
#include "retromanager/parsers/CfgDocument.hpp"
#include "retromanager/services/RetroArchPaths.hpp"
#include "retromanager/services/ThumbnailManager.hpp"

using namespace rm;

namespace {

const std::string kBoxartUrl = "ftp://mock.local/shop/boxart/nds/platine.png";
const char* kRomPath = "/roms/nds/Pokemon Platine (France).nds";
const char* kThumbnail = "/retroarch/thumbnails/Nintendo - Nintendo DS/Named_Boxarts/Pokemon Platine (France).png";

// Smallest meaningful PNG prefix: signature + IHDR chunk start.
const std::string kPng = std::string("\x89PNG\r\n\x1a\n", 8) + std::string("\0\0\0\rIHDR", 8) + std::string(200, 'x');

GameEntry platine() {
    GameEntry game;
    game.id = "nds/Pokemon Platine (France).nds";
    game.title = "Pokémon Platine";
    game.system = "nds";
    game.fileName = "Pokemon Platine (France).nds";
    game.boxartUrl = kBoxartUrl;
    return game;
}

struct Fixture {
    std::unique_ptr<test::MemoryFileSystem> sd = test::makeMockSdCard();
    MockRemoteSource shop{"{}", ""};
    ThumbnailManager thumbnails{*sd, SdLayout{}, shop};
    CancellationToken cancel;
};

}  // namespace

TEST(RetroArchPaths, ThumbnailNamesFollowRetroArchsRule) {
    EXPECT_EQ(retroarch::thumbnailName("Pokemon Platine (France)"), "Pokemon Platine (France)");
    EXPECT_EQ(retroarch::thumbnailName("Tom & Jerry: The Movie?"), "Tom _ Jerry_ The Movie_");
    EXPECT_EQ(retroarch::thumbnailName("a*b/c\\d<e>f|g`h"), "a_b_c_d_e_f_g_h");
}

TEST(ThumbnailManager, InstallsTheBoxartWhereRetroArchLooksForIt) {
    Fixture f;
    f.shop.addFile(kBoxartUrl, kPng);

    auto result = f.thumbnails.run(platine(), kRomPath, f.cancel);
    ASSERT_TRUE(result.has_value());
    ASSERT_TRUE(result->ok()) << result->error().describe();
    EXPECT_EQ(f.sd->readFile(kThumbnail).value(), kPng);
    EXPECT_EQ(f.thumbnails.destinationFor(platine(), kRomPath).value(), kThumbnail);
}

TEST(ThumbnailManager, NamesTheFileAfterThePlaylistLabel) {
    Fixture f;
    f.shop.addFile(kBoxartUrl, kPng);
    GameEntry game = platine();
    game.fileName = "Tom & Jerry.nds";
    ASSERT_TRUE(f.thumbnails.run(game, "/roms/nds/Tom & Jerry.nds", f.cancel)->ok());
    EXPECT_TRUE(f.sd->isFile("/retroarch/thumbnails/Nintendo - Nintendo DS/Named_Boxarts/Tom _ Jerry.png"));
}

TEST(ThumbnailManager, SkipsGamesWithoutBoxart) {
    Fixture f;
    GameEntry game = platine();
    game.boxartUrl.clear();
    EXPECT_FALSE(f.thumbnails.run(game, kRomPath, f.cancel).has_value());
    EXPECT_EQ(f.shop.downloadCount(), 0);
}

TEST(ThumbnailManager, RefusesWhatIsNotAPngAndWritesNothing) {
    Fixture f;
    f.shop.addFile(kBoxartUrl, "<html>404 Not Found</html>");
    EXPECT_EQ(f.thumbnails.run(platine(), kRomPath, f.cancel)->error().code, ErrorCode::IntegrityError);
    EXPECT_FALSE(f.sd->exists(kThumbnail));

    // RetroArch only loads .png thumbnails: a JPEG would never be shown.
    f.shop.addFile(kBoxartUrl, std::string("\xFF\xD8\xFF\xE0", 4) + std::string(100, 'j'));
    EXPECT_EQ(f.thumbnails.run(platine(), kRomPath, f.cancel)->error().code, ErrorCode::Unsupported);
    EXPECT_FALSE(f.sd->exists(kThumbnail));
}

TEST(ThumbnailManager, CapsTheImageSize) {
    Fixture f;
    f.shop.addFile(kBoxartUrl, kPng + std::string(ThumbnailManager::kMaxImageBytes, 'x'));
    EXPECT_FALSE(f.thumbnails.run(platine(), kRomPath, f.cancel)->ok());
    EXPECT_FALSE(f.sd->exists(kThumbnail));
}

TEST(ThumbnailManager, HonoursRetroArchsThumbnailsDirectory) {
    Fixture f;
    f.shop.addFile(kBoxartUrl, kPng);
    CfgDocument cfg = CfgDocument::parse(f.sd->readFile("/retroarch/retroarch.cfg").value());
    ASSERT_TRUE(cfg.set("thumbnails_directory", "/media/thumbs").ok());
    ASSERT_TRUE(f.sd->writeFile("/retroarch/retroarch.cfg", cfg.serialize()).ok());

    ASSERT_TRUE(f.thumbnails.run(platine(), kRomPath, f.cancel)->ok());
    EXPECT_TRUE(f.sd->isFile("/media/thumbs/Nintendo - Nintendo DS/Named_Boxarts/Pokemon Platine (France).png"));
}

TEST(ThumbnailManager, ReportsNetworkErrorsAndMissingRetroArch) {
    Fixture f;  // boxart not on the server
    EXPECT_EQ(f.thumbnails.run(platine(), kRomPath, f.cancel)->error().code, ErrorCode::NotFound);

    test::MemoryFileSystem empty;
    MockRemoteSource shop("{}", "");
    shop.addFile(kBoxartUrl, kPng);
    ThumbnailManager noRetroArch(empty, SdLayout{}, shop);
    EXPECT_EQ(noRetroArch.run(platine(), kRomPath, f.cancel)->error().code, ErrorCode::NotFound);
    EXPECT_EQ(empty.nodeCount(), 0u);
    EXPECT_EQ(shop.downloadCount(), 0);  // checked before contacting the NAS
}

// --- libretro scraper (fallback) -------------------------------------------

namespace {

const std::string kScraper = "https://thumbnails.libretro.com/";
const std::string kScraped =
    "https://thumbnails.libretro.com/Nintendo%20-%20Nintendo%20DS/Named_Boxarts/Pokemon%20Platine%20(France).png";

struct ScraperFixture {
    std::unique_ptr<test::MemoryFileSystem> sd = test::makeMockSdCard();
    MockRemoteSource web{"{}", ""};
    ThumbnailManager thumbnails{*sd, SdLayout{}, web, kScraper};
    CancellationToken cancel;
};

}  // namespace

TEST(ThumbnailScraper, ForgesTheLibretroUrl) {
    ScraperFixture f;
    EXPECT_EQ(f.thumbnails.scraperUrlFor(platine(), kRomPath).value(),
              "https://thumbnails.libretro.com/Nintendo%20-%20Nintendo%20DS/Named_Boxarts/Pokemon%20Platine%20%28France%29.png");
    GameEntry tricky = platine();
    EXPECT_EQ(f.thumbnails.scraperUrlFor(tricky, "/roms/nds/Tom & Jerry: Frenzy #1.nds").value(),
              "https://thumbnails.libretro.com/Nintendo%20-%20Nintendo%20DS/Named_Boxarts/Tom%20_%20Jerry_%20Frenzy%20%231.png");
    ThumbnailManager noSlash(*f.sd, SdLayout{}, f.web, "https://mirror.example/thumbs");
    EXPECT_EQ(noSlash.scraperUrlFor(platine(), kRomPath).value().rfind("https://mirror.example/thumbs/Nintendo", 0), 0u);
    GameEntry arcade = platine();
    arcade.system = "arcade";
    EXPECT_FALSE(f.thumbnails.scraperUrlFor(arcade, "/roms/arcade/sf2.zip").ok());
}

TEST(ThumbnailScraper, UsedWhenTheIndexHasNoBoxart) {
    ScraperFixture f;
    f.web.addFile(kScraped, kPng);
    GameEntry game = platine();
    game.boxartUrl.clear();

    auto result = f.thumbnails.run(game, kRomPath, f.cancel);
    ASSERT_TRUE(result.has_value());
    ASSERT_TRUE(result->ok()) << result->error().describe();
    EXPECT_EQ(f.sd->readFile(kThumbnail).value(), kPng);
}

TEST(ThumbnailScraper, UsedWhenTheIndexBoxartFails) {
    ScraperFixture f;
    f.web.addFile(kScraped, kPng);  // the shop's own boxart URL is missing on the server
    auto result = f.thumbnails.run(platine(), kRomPath, f.cancel);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->ok());
    EXPECT_TRUE(f.sd->isFile(kThumbnail));
}

TEST(ThumbnailScraper, A404IsIgnoredDiscreetly) {
    ScraperFixture f;
    GameEntry game = platine();
    game.boxartUrl.clear();
    EXPECT_FALSE(f.thumbnails.run(game, kRomPath, f.cancel).has_value());  // nothing to report
    EXPECT_EQ(f.web.downloadCount(), 1);
    EXPECT_FALSE(f.sd->exists(kThumbnail));

    // With a boxart in the index, its own error is what the user sees.
    auto failed = f.thumbnails.run(platine(), kRomPath, f.cancel);
    ASSERT_TRUE(failed.has_value());
    EXPECT_EQ(failed->error().code, ErrorCode::NotFound);
}

TEST(ThumbnailScraper, TheIndexBoxartWins) {
    ScraperFixture f;
    f.web.addFile(kBoxartUrl, kPng);
    f.web.addFile(kScraped, std::string("\x89PNG\r\n\x1a\n", 8) + "other");
    ASSERT_TRUE(f.thumbnails.run(platine(), kRomPath, f.cancel)->ok());
    EXPECT_EQ(f.sd->readFile(kThumbnail).value(), kPng);
    EXPECT_EQ(f.web.downloadCount(), 1);  // the scraper was not needed
}

TEST(ThumbnailScraper, SilentWithoutRetroArch) {
    test::MemoryFileSystem empty;
    MockRemoteSource web("{}", "");
    ThumbnailManager thumbnails(empty, SdLayout{}, web, kScraper);
    GameEntry game = platine();
    game.boxartUrl.clear();
    CancellationToken cancel;
    EXPECT_FALSE(thumbnails.run(game, kRomPath, cancel).has_value());
    EXPECT_EQ(web.downloadCount(), 0);
}
