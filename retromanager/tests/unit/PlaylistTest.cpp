#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include "MemoryFileSystem.hpp"
#include "MockSdCard.hpp"
#include "retromanager/parsers/CfgDocument.hpp"
#include "retromanager/parsers/PlaylistDocument.hpp"
#include "retromanager/services/PlaylistManager.hpp"

using namespace rm;

namespace {

PlaylistItem item(std::string path, std::string label, std::string crc = "1A2B3C4D|crc") {
    PlaylistItem entry;
    entry.path = std::move(path);
    entry.label = std::move(label);
    entry.crc32 = std::move(crc);
    entry.dbName = "Nintendo - Nintendo DS.lpl";
    return entry;
}

// As written by RetroArch 1.19 (manual content scan), with fields
// RetroManager does not know about.
const char* kRetroArchPlaylist = R"lpl({
  "version": "1.5",
  "default_core_path": "/retroarch/cores/melonds_libretro_libnx.nro",
  "default_core_name": "Nintendo - DS (melonDS)",
  "base_content_directory": "",
  "label_display_mode": 0,
  "right_thumbnail_mode": 0,
  "left_thumbnail_mode": 0,
  "thumbnail_match_mode": 0,
  "sort_mode": 0,
  "scan_content_dir": "/roms/nds",
  "items": [
    {
      "path": "/roms/nds/Mario Kart DS (Europe).nds",
      "entry_slot": 0,
      "label": "Mario Kart DS",
      "core_path": "DETECT",
      "core_name": "DETECT",
      "crc32": "DETECT",
      "db_name": "Nintendo - Nintendo DS.lpl",
      "runtime_hours": 3
    }
  ]
})lpl";

const char* kPlaylist = "/retroarch/playlists/Nintendo - Nintendo DS.lpl";
const char* kRomPath = "/roms/nds/Pokemon Platine (France).nds";

GameEntry platine() {
    GameEntry game;
    game.id = "nds/Pokemon Platine (France).nds";
    game.title = "Pokémon Platine";
    game.system = "nds";
    game.fileName = "Pokemon Platine (France).nds";
    game.crc32 = "1a2b3c4d";
    return game;
}

nlohmann::json readJson(IFileSystem& fs, const std::string& path) {
    return nlohmann::json::parse(fs.readFile(path).value());
}

}  // namespace

// --- PlaylistDocument ----------------------------------------------------

TEST(PlaylistDocument, EmptyInputIsANewPlaylistInRetroArchsFormat) {
    auto doc = PlaylistDocument::parse("");
    ASSERT_TRUE(doc.ok()) << doc.error().describe();
    EXPECT_TRUE(doc.value().items().empty());
    ASSERT_TRUE(doc.value().upsert(item("/roms/nds/A.nds", "A")));

    EXPECT_EQ(doc.value().serialize(),
              "{\n"
              "  \"version\": \"1.5\",\n"
              "  \"default_core_path\": \"\",\n"
              "  \"default_core_name\": \"\",\n"
              "  \"label_display_mode\": 0,\n"
              "  \"right_thumbnail_mode\": 0,\n"
              "  \"left_thumbnail_mode\": 0,\n"
              "  \"sort_mode\": 0,\n"
              "  \"items\": [\n"
              "    {\n"
              "      \"path\": \"/roms/nds/A.nds\",\n"
              "      \"label\": \"A\",\n"
              "      \"core_path\": \"DETECT\",\n"
              "      \"core_name\": \"DETECT\",\n"
              "      \"crc32\": \"1A2B3C4D|crc\",\n"
              "      \"db_name\": \"Nintendo - Nintendo DS.lpl\"\n"
              "    }\n"
              "  ]\n"
              "}\n");
}

TEST(PlaylistDocument, RoundTrips) {
    PlaylistDocument doc = PlaylistDocument::parse("").value();
    doc.upsert(item("/roms/nds/A.nds", "A"));
    doc.upsert(item("/roms/nds/Pokémon Émeraude.nds", "Pokémon Émeraude", "DETECT"));
    auto again = PlaylistDocument::parse(doc.serialize());
    ASSERT_TRUE(again.ok()) << again.error().describe();
    EXPECT_EQ(again.value().items(), doc.items());
    EXPECT_EQ(again.value().serialize(), doc.serialize());
}

TEST(PlaylistDocument, SamePathIsUpdatedNotDuplicated) {
    PlaylistDocument doc = PlaylistDocument::parse("").value();
    EXPECT_TRUE(doc.upsert(item("/roms/nds/A.nds", "A", "DETECT")));
    EXPECT_FALSE(doc.upsert(item("/roms/nds/A.nds", "A", "DETECT")));  // nothing new
    EXPECT_TRUE(doc.upsert(item("/roms/nds/A.nds", "A", "1A2B3C4D|crc")));  // re-downloaded: CRC now known
    // RetroArch may have stored the same file as "sdmc:/..." or with another case.
    EXPECT_FALSE(doc.upsert(item("sdmc:/ROMS/nds/a.nds", "A", "1A2B3C4D|crc")));
    ASSERT_EQ(doc.items().size(), 1u);
    EXPECT_EQ(doc.items()[0].crc32, "1A2B3C4D|crc");
    EXPECT_EQ(doc.items()[0].path, "/roms/nds/A.nds");  // the stored spelling is kept
}

TEST(PlaylistDocument, KeepsWhatRetroArchWroteIncludingUnknownFields) {
    auto doc = PlaylistDocument::parse(kRetroArchPlaylist);
    ASSERT_TRUE(doc.ok()) << doc.error().describe();
    ASSERT_EQ(doc.value().items().size(), 1u);
    EXPECT_EQ(doc.value().items()[0].label, "Mario Kart DS");

    ASSERT_TRUE(doc.value().upsert(item(kRomPath, "Pokemon Platine (France)")));

    nlohmann::json before = nlohmann::json::parse(kRetroArchPlaylist);
    nlohmann::ordered_json after = nlohmann::ordered_json::parse(doc.value().serialize());
    EXPECT_EQ(after["default_core_name"], "Nintendo - DS (melonDS)");
    EXPECT_EQ(after["scan_content_dir"], "/roms/nds");
    ASSERT_EQ(after["items"].size(), 2u);
    EXPECT_EQ(nlohmann::json(after["items"][0]), before["items"][0]);  // untouched, runtime_hours included
    EXPECT_EQ(after["items"][1]["path"], kRomPath);
    // Key order of the original document is preserved (diff-friendly).
    EXPECT_EQ(after.begin().key(), "version");
    EXPECT_EQ(doc.value().serialize().find("\"default_core_path\""), std::string(kRetroArchPlaylist).find("\"default_core_path\""));
}

TEST(PlaylistDocument, ReadsTheLegacySixLineFormatAndWritesJson) {
    const std::string legacy =
        "/roms/nds/Mario Kart DS (Europe).nds\n"
        "Mario Kart DS\n"
        "DETECT\n"
        "DETECT\n"
        "DETECT\n"
        "Nintendo - Nintendo DS.lpl\n"
        "/roms/nds/Zelda.nds\r\n"
        "Zelda\r\n"
        "/retroarch/cores/desmume_libretro_libnx.nro\r\n"
        "DeSmuME\r\n"
        "0BADF00D|crc\r\n"
        "Nintendo - Nintendo DS.lpl\r\n";
    auto doc = PlaylistDocument::parse(legacy);
    ASSERT_TRUE(doc.ok()) << doc.error().describe();
    EXPECT_TRUE(doc.value().wasLegacy());
    ASSERT_EQ(doc.value().items().size(), 2u);
    EXPECT_EQ(doc.value().items()[1].label, "Zelda");
    EXPECT_EQ(doc.value().items()[1].corePath, "/retroarch/cores/desmume_libretro_libnx.nro");
    EXPECT_EQ(doc.value().items()[1].crc32, "0BADF00D|crc");

    auto converted = PlaylistDocument::parse(doc.value().serialize());
    ASSERT_TRUE(converted.ok());
    EXPECT_FALSE(converted.value().wasLegacy());
    EXPECT_EQ(converted.value().items(), doc.value().items());
}

TEST(PlaylistDocument, RejectsWhatItCannotUnderstand) {
    EXPECT_EQ(PlaylistDocument::parse("{").error().code, ErrorCode::ParseError);
    EXPECT_EQ(PlaylistDocument::parse(R"({"items": 3})").error().code, ErrorCode::ParseError);
    EXPECT_EQ(PlaylistDocument::parse(R"({"items": [{"path": 3}]})").error().code, ErrorCode::ParseError);
    EXPECT_EQ(PlaylistDocument::parse("[]").error().code, ErrorCode::ParseError);
    EXPECT_EQ(PlaylistDocument::parse("/roms/a.nds\nA\nDETECT\n").error().code, ErrorCode::ParseError);  // truncated legacy entry
}

TEST(PlaylistDocument, CrcFieldFormat) {
    EXPECT_EQ(PlaylistDocument::crcField("1a2b3c4d"), "1A2B3C4D|crc");
    EXPECT_EQ(PlaylistDocument::crcField(""), "DETECT");
    EXPECT_EQ(PlaylistDocument::crcField("xyz"), "DETECT");
}

// --- PlaylistManager -----------------------------------------------------

TEST(PlaylistManager, AddsTheGameToItsSystemPlaylist) {
    auto sd = test::makeMockSdCard();
    PlaylistManager playlists(*sd, SdLayout{});
    CancellationToken cancel;

    auto result = playlists.run(platine(), kRomPath, cancel);
    ASSERT_TRUE(result.has_value());
    ASSERT_TRUE(result->ok()) << result->error().describe();

    nlohmann::json lpl = readJson(*sd, kPlaylist);
    ASSERT_EQ(lpl["items"].size(), 1u);
    EXPECT_EQ(lpl["items"][0]["path"], kRomPath);
    EXPECT_EQ(lpl["items"][0]["label"], "Pokemon Platine (France)");  // file name without extension
    EXPECT_EQ(lpl["items"][0]["core_path"], "DETECT");
    EXPECT_EQ(lpl["items"][0]["core_name"], "DETECT");
    EXPECT_EQ(lpl["items"][0]["crc32"], "1A2B3C4D|crc");
    EXPECT_EQ(lpl["items"][0]["db_name"], "Nintendo - Nintendo DS.lpl");
    EXPECT_EQ(playlists.playlistPathFor(platine()).value(), kPlaylist);
}

TEST(PlaylistManager, ReinstallingDoesNotDuplicateAndOtherGamesAreAppended) {
    auto sd = test::makeMockSdCard();
    PlaylistManager playlists(*sd, SdLayout{});
    CancellationToken cancel;
    ASSERT_TRUE(playlists.run(platine(), kRomPath, cancel)->ok());
    ASSERT_TRUE(playlists.run(platine(), kRomPath, cancel)->ok());
    GameEntry other = platine();
    other.fileName = "Other.nds";
    ASSERT_TRUE(playlists.run(other, "/roms/nds/Other.nds", cancel)->ok());

    nlohmann::json lpl = readJson(*sd, kPlaylist);
    ASSERT_EQ(lpl["items"].size(), 2u);
    EXPECT_EQ(lpl["items"][1]["label"], "Other");
}

TEST(PlaylistManager, MergesIntoRetroArchsOwnPlaylistAndKeepsABackup) {
    auto sd = test::makeMockSdCard();
    ASSERT_TRUE(sd->writeFile(kPlaylist, kRetroArchPlaylist).ok());
    PlaylistManager playlists(*sd, SdLayout{});
    CancellationToken cancel;

    ASSERT_TRUE(playlists.run(platine(), kRomPath, cancel)->ok());

    nlohmann::json lpl = readJson(*sd, kPlaylist);
    ASSERT_EQ(lpl["items"].size(), 2u);
    EXPECT_EQ(lpl["items"][0]["label"], "Mario Kart DS");
    EXPECT_EQ(lpl["default_core_name"], "Nintendo - DS (melonDS)");
    EXPECT_EQ(sd->readFile(std::string(kPlaylist) + ".rmbak").value(), kRetroArchPlaylist);

    // Already listed: nothing is written at all.
    sd->setReadOnly(true);
    EXPECT_TRUE(playlists.run(platine(), kRomPath, cancel)->ok());
}

TEST(PlaylistManager, HonoursRetroArchsPlaylistDirectory) {
    auto sd = test::makeMockSdCard();
    CfgDocument cfg = CfgDocument::parse(sd->readFile("/retroarch/retroarch.cfg").value());
    ASSERT_TRUE(cfg.set("playlist_directory", "/custom/playlists").ok());
    ASSERT_TRUE(sd->writeFile("/retroarch/retroarch.cfg", cfg.serialize()).ok());
    PlaylistManager playlists(*sd, SdLayout{});
    CancellationToken cancel;

    ASSERT_TRUE(playlists.run(platine(), kRomPath, cancel)->ok());
    EXPECT_TRUE(sd->isFile("/custom/playlists/Nintendo - Nintendo DS.lpl"));
    EXPECT_FALSE(sd->exists(kPlaylist));
}

TEST(PlaylistManager, CorruptedPlaylistIsLeftAlone) {
    auto sd = test::makeMockSdCard();
    ASSERT_TRUE(sd->writeFile(kPlaylist, "{\"items\": [").ok());
    PlaylistManager playlists(*sd, SdLayout{});
    CancellationToken cancel;

    auto result = playlists.run(platine(), kRomPath, cancel);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->error().code, ErrorCode::ParseError);
    EXPECT_EQ(sd->readFile(kPlaylist).value(), "{\"items\": [");
}

TEST(PlaylistManager, NeedsRetroArchAndAKnownSystem) {
    test::MemoryFileSystem empty;
    PlaylistManager noRetroArch(empty, SdLayout{});
    CancellationToken cancel;
    EXPECT_EQ(noRetroArch.run(platine(), kRomPath, cancel)->error().code, ErrorCode::NotFound);
    EXPECT_EQ(empty.nodeCount(), 0u);

    auto sd = test::makeMockSdCard();
    PlaylistManager playlists(*sd, SdLayout{});
    GameEntry arcade = platine();
    arcade.system = "arcade";  // FBNeo and MAME use different databases
    EXPECT_EQ(playlists.run(arcade, "/roms/arcade/sf2.zip", cancel)->error().code, ErrorCode::Unsupported);
}
