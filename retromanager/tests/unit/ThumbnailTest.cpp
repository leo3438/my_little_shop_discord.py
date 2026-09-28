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
