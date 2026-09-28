#include <gtest/gtest.h>

#include "retromanager/parsers/RepoIndexParser.hpp"

using namespace rm;

namespace {

RepoIndex parseOk(std::string_view json, std::string baseUrl = "") {
    auto result = RepoIndexParser(std::move(baseUrl)).parse(json);
    EXPECT_TRUE(result.ok()) << (result.ok() ? "" : result.error().describe());
    return result.ok() ? result.value() : RepoIndex{};
}

Error parseError(std::string_view json) {
    auto result = RepoIndexParser().parse(json);
    EXPECT_FALSE(result.ok()) << "document should have been rejected: " << json;
    return result.ok() ? Error{ErrorCode::IoError, "unexpected success"} : result.error();
}

}  // namespace

// --- valid documents -----------------------------------------------------

TEST(RepoIndexParser, ParsesAFullyDescribedEntry) {
    RepoIndex index = parseOk(R"({
        "version": 1,
        "games": [{
            "id": "pkmn-platine-fr",
            "title": "Pokémon Platine",
            "system": "nds",
            "region": "EUR",
            "size": 134217728,
            "url": "ftp://nas.local/roms/nds/Pokemon%20Platine%20(France).nds",
            "boxart": "https://images.example/nds/pokemon-platine.png",
            "crc32": "9A2D6A7E",
            "year": 2009,
            "description": "Version française."
        }]
    })");

    ASSERT_EQ(index.games.size(), 1u);
    GameEntry expected;
    expected.id = "pkmn-platine-fr";
    expected.title = "Pokémon Platine";
    expected.system = "nds";
    expected.region = "EUR";
    expected.sizeBytes = 134217728;
    expected.romUrl = "ftp://nas.local/roms/nds/Pokemon%20Platine%20(France).nds";
    expected.fileName = "Pokemon Platine (France).nds";
    expected.boxartUrl = "https://images.example/nds/pokemon-platine.png";
    expected.crc32 = "9a2d6a7e";
    expected.year = 2009;
    expected.description = "Version française.";
    EXPECT_EQ(index.games[0], expected);
    EXPECT_TRUE(index.warnings.empty());
}

TEST(RepoIndexParser, ReadsShopMetadata) {
    RepoIndex index = parseOk(R"({"name": "NAS de Léo", "success": "Bienvenue !", "games": []})");
    EXPECT_EQ(index.name, "NAS de Léo");
    EXPECT_EQ(index.motd, "Bienvenue !");
    EXPECT_TRUE(index.games.empty());
}

TEST(RepoIndexParser, DerivesMissingFieldsFromTheUrl) {
    RepoIndex index = parseOk(R"({"games": [
        {"url": "ftp://nas/roms/snes/Super%20Mario%20World%20(USA).sfc"}
    ]})");

    ASSERT_EQ(index.games.size(), 1u);
    const GameEntry& game = index.games[0];
    EXPECT_EQ(game.title, "Super Mario World (USA)");
    EXPECT_EQ(game.fileName, "Super Mario World (USA).sfc");
    EXPECT_EQ(game.system, "snes");
    EXPECT_EQ(game.id, "snes/Super Mario World (USA).sfc");
    EXPECT_EQ(game.sizeBytes, 0u);
    EXPECT_FALSE(game.year.has_value());
}

TEST(RepoIndexParser, UnknownExtensionGivesUnknownSystem) {
    RepoIndex index = parseOk(R"({"games": [{"url": "ftp://nas/roms/Some Game.zip"}]})");
    ASSERT_EQ(index.games.size(), 1u);
    EXPECT_EQ(index.games[0].system, "unknown");
}

TEST(RepoIndexParser, ExplicitSystemWinsAndIsLowercased) {
    RepoIndex index = parseOk(R"({"games": [{"url": "ftp://nas/a.zip", "system": "MegaDrive"}]})");
    ASSERT_EQ(index.games.size(), 1u);
    EXPECT_EQ(index.games[0].system, "megadrive");
}

TEST(RepoIndexParser, AcceptsTinfoilStyleFilesArray) {
    // Tinfoil puts the display file name in the URL fragment.
    RepoIndex index = parseOk(R"({
        "success": "hello",
        "files": [{"url": "https://nas/dl?id=42#Advance%20Wars%20(USA).gba", "size": 4194304}]
    })");

    ASSERT_EQ(index.games.size(), 1u);
    const GameEntry& game = index.games[0];
    EXPECT_EQ(game.romUrl, "https://nas/dl?id=42");
    EXPECT_EQ(game.fileName, "Advance Wars (USA).gba");
    EXPECT_EQ(game.title, "Advance Wars (USA)");
    EXPECT_EQ(game.system, "gba");
    EXPECT_EQ(game.sizeBytes, 4194304u);
    EXPECT_EQ(index.motd, "hello");
}

TEST(RepoIndexParser, MergesGamesThenFilesInOrder) {
    RepoIndex index = parseOk(R"({
        "games": [{"url": "ftp://n/b.sfc"}, {"url": "ftp://n/a.sfc"}],
        "files": [{"url": "ftp://n/c.gba"}]
    })");
    ASSERT_EQ(index.games.size(), 3u);
    EXPECT_EQ(index.games[0].fileName, "b.sfc");
    EXPECT_EQ(index.games[1].fileName, "a.sfc");
    EXPECT_EQ(index.games[2].fileName, "c.gba");
}

TEST(RepoIndexParser, ResolvesRelativeUrlsAgainstTheIndexLocation) {
    RepoIndex index = parseOk(R"({"games": [
        {"url": "roms/a.sfc", "boxart": "../art/a.png"},
        {"url": "/roms/b.sfc"}
    ]})",
                              "ftp://nas.local/shop/index.json");

    ASSERT_EQ(index.games.size(), 2u);
    EXPECT_EQ(index.games[0].romUrl, "ftp://nas.local/shop/roms/a.sfc");
    EXPECT_EQ(index.games[0].boxartUrl, "ftp://nas.local/art/a.png");
    EXPECT_EQ(index.games[1].romUrl, "ftp://nas.local/roms/b.sfc");
}

TEST(RepoIndexParser, SkipsRelativeUrlsWithoutBase) {
    RepoIndex index = parseOk(R"({"games": [{"url": "roms/a.sfc"}, {"url": "ftp://n/b.sfc"}]})");
    ASSERT_EQ(index.games.size(), 1u);
    EXPECT_EQ(index.games[0].fileName, "b.sfc");
    EXPECT_EQ(index.warnings.size(), 1u);
}

TEST(RepoIndexParser, IgnoresUnknownFieldsForForwardCompatibility) {
    RepoIndex index = parseOk(R"({
        "future_top_level": {"x": 1},
        "games": [{"url": "ftp://n/a.sfc", "rating": 5, "tags": ["rpg"]}]
    })");
    EXPECT_EQ(index.games.size(), 1u);
    EXPECT_TRUE(index.warnings.empty());
}

TEST(RepoIndexParser, AcceptsSizesBeyond4GiB) {
    RepoIndex index = parseOk(R"({"games": [{"url": "ftp://n/a.pbp", "size": 5368709120}]})");
    ASSERT_EQ(index.games.size(), 1u);
    EXPECT_EQ(index.games[0].sizeBytes, 5368709120ull);
}

TEST(RepoIndexParser, AcceptsVersionOneAndMissingVersion) {
    EXPECT_TRUE(RepoIndexParser().parse(R"({"version": 1, "games": []})").ok());
    EXPECT_TRUE(RepoIndexParser().parse(R"({"games": []})").ok());
}

TEST(RepoIndexParser, StripsUtf8ByteOrderMark) {
    RepoIndex index = parseOk("\xEF\xBB\xBF{\"games\": [{\"url\": \"ftp://n/a.sfc\"}]}");
    EXPECT_EQ(index.games.size(), 1u);
}

TEST(RepoIndexParser, ReadsOptionalCheatUrl) {
    RepoIndex index = parseOk(R"({"games": [
        {"url": "roms/nds/a.nds", "cheat_url": "cheats/nds/a.cht"},
        {"url": "roms/nds/b.nds", "cheat_url": "https://cheats.example/b.cht"},
        {"url": "roms/nds/c.nds"}
    ]})",
                              "ftp://nas/shop/index.json");
    ASSERT_EQ(index.games.size(), 3u);
    EXPECT_EQ(index.games[0].cheatUrl, "ftp://nas/shop/cheats/nds/a.cht");
    EXPECT_EQ(index.games[1].cheatUrl, "https://cheats.example/b.cht");
    EXPECT_EQ(index.games[2].cheatUrl, "");
}

TEST(RepoIndexParser, SkipsEntriesWithInvalidCheatUrl) {
    RepoIndex index = parseOk(R"({"games": [
        {"url": "ftp://n/a.nds", "cheat_url": 12},
        {"url": "ftp://n/b.nds", "cheat_url": "relative/without/base.cht"}
    ]})");
    EXPECT_TRUE(index.games.empty());
    EXPECT_EQ(index.warnings.size(), 2u);
}

// --- rejected documents --------------------------------------------------

TEST(RepoIndexParser, RejectsMalformedJsonWithPosition) {
    Error error = parseError(R"({"games": [ {"url": "ftp://n/a.sfc"}, ]})");
    EXPECT_EQ(error.code, ErrorCode::ParseError);
    EXPECT_NE(error.message.find("line"), std::string::npos) << error.message;
}

TEST(RepoIndexParser, RejectsTruncatedDocument) {
    EXPECT_EQ(parseError(R"({"games": [{"url": "ftp://n/a.sfc"})").code, ErrorCode::ParseError);
}

TEST(RepoIndexParser, RejectsEmptyDocument) {
    EXPECT_EQ(parseError("").code, ErrorCode::ParseError);
    EXPECT_EQ(parseError("   \n").code, ErrorCode::ParseError);
}

TEST(RepoIndexParser, RejectsNonObjectRoot) {
    EXPECT_EQ(parseError("[]").code, ErrorCode::ParseError);
    EXPECT_EQ(parseError("\"games\"").code, ErrorCode::ParseError);
    EXPECT_EQ(parseError("null").code, ErrorCode::ParseError);
}

TEST(RepoIndexParser, RejectsDocumentWithoutGames) {
    EXPECT_EQ(parseError(R"({"name": "empty shop"})").code, ErrorCode::ParseError);
}

TEST(RepoIndexParser, RejectsNonArrayGames) {
    EXPECT_EQ(parseError(R"({"games": {"url": "ftp://n/a.sfc"}})").code, ErrorCode::ParseError);
    EXPECT_EQ(parseError(R"({"files": "nope"})").code, ErrorCode::ParseError);
}

TEST(RepoIndexParser, RejectsNewerFormatVersion) {
    EXPECT_EQ(parseError(R"({"version": 2, "games": []})").code, ErrorCode::Unsupported);
}

TEST(RepoIndexParser, RejectsInvalidVersionType) {
    EXPECT_EQ(parseError(R"({"version": "1", "games": []})").code, ErrorCode::ParseError);
    EXPECT_EQ(parseError(R"({"version": 0, "games": []})").code, ErrorCode::ParseError);
}

TEST(RepoIndexParser, RejectsInvalidUtf8) {
    EXPECT_EQ(parseError("{\"games\": [{\"url\": \"ftp://n/\xC3\x28.sfc\"}]}").code, ErrorCode::ParseError);
}

TEST(RepoIndexParser, RejectsWrongMetadataTypes) {
    EXPECT_EQ(parseError(R"({"name": 42, "games": []})").code, ErrorCode::ParseError);
}

// --- lenient entry handling ----------------------------------------------

TEST(RepoIndexParser, SkipsNonObjectEntries) {
    RepoIndex index = parseOk(R"({"games": ["ftp://n/a.sfc", 12, null, {"url": "ftp://n/b.sfc"}]})");
    ASSERT_EQ(index.games.size(), 1u);
    EXPECT_EQ(index.games[0].fileName, "b.sfc");
    EXPECT_EQ(index.warnings.size(), 3u);
}

TEST(RepoIndexParser, SkipsEntriesWithoutUsableUrl) {
    RepoIndex index = parseOk(R"({"games": [
        {"title": "no url"},
        {"url": ""},
        {"url": 12},
        {"url": "ftp://n/"},
        {"url": "ftp://n/ok.sfc"}
    ]})");
    ASSERT_EQ(index.games.size(), 1u);
    EXPECT_EQ(index.warnings.size(), 4u);
}

TEST(RepoIndexParser, SkipsEntriesWithInvalidFieldTypes) {
    RepoIndex index = parseOk(R"({"games": [
        {"url": "ftp://n/1.sfc", "title": 42},
        {"url": "ftp://n/2.sfc", "size": -1},
        {"url": "ftp://n/3.sfc", "size": "1024"},
        {"url": "ftp://n/4.sfc", "size": 10.5},
        {"url": "ftp://n/5.sfc", "year": 1850},
        {"url": "ftp://n/6.sfc", "crc32": "xyz"},
        {"url": "ftp://n/7.sfc", "region": ["EUR"]},
        {"url": "ftp://n/8.sfc", "boxart": false},
        {"url": "ftp://n/9.sfc", "title": "  "},
        {"url": "ftp://n/ok.sfc", "size": 0, "year": 1990}
    ]})");
    ASSERT_EQ(index.games.size(), 1u);
    EXPECT_EQ(index.games[0].fileName, "ok.sfc");
    EXPECT_EQ(index.warnings.size(), 9u);
}

TEST(RepoIndexParser, WarningsNameTheFaultyEntry) {
    RepoIndex index = parseOk(R"({"games": [{"url": "ftp://n/ok.sfc"}, {"url": "ftp://n/2.sfc", "size": -1}]})");
    ASSERT_EQ(index.warnings.size(), 1u);
    EXPECT_NE(index.warnings[0].find("games[1]"), std::string::npos) << index.warnings[0];
    EXPECT_NE(index.warnings[0].find("size"), std::string::npos) << index.warnings[0];
}

TEST(RepoIndexParser, KeepsFirstOfDuplicateIds) {
    RepoIndex index = parseOk(R"({"games": [
        {"id": "x", "url": "ftp://n/first.sfc"},
        {"id": "x", "url": "ftp://n/second.sfc"},
        {"url": "ftp://n/roms/a.sfc"},
        {"url": "ftp://n/other/a.sfc"}
    ]})");
    ASSERT_EQ(index.games.size(), 2u);
    EXPECT_EQ(index.games[0].fileName, "first.sfc");
    EXPECT_EQ(index.games[1].romUrl, "ftp://n/roms/a.sfc");
    EXPECT_EQ(index.warnings.size(), 2u);
}

TEST(RepoIndexParser, WarnsAboutUnsupportedTinfoilDirectories) {
    RepoIndex index = parseOk(R"({"directories": ["ftp://n/more/"], "files": []})");
    EXPECT_EQ(index.warnings.size(), 1u);
}
