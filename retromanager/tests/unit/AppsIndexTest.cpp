#include <gtest/gtest.h>

#include "retromanager/parsers/RepoIndexParser.hpp"

using namespace rm;

namespace {

RepoIndex parseOk(std::string_view json) {
    auto result = RepoIndexParser("ftp://nas.local/shop/index.json").parse(json);
    EXPECT_TRUE(result.ok()) << (result.ok() ? "" : result.error().describe());
    return result.ok() ? result.value() : RepoIndex{};
}

}  // namespace

TEST(AppsIndex, ParsesAFullyDescribedApp) {
    RepoIndex index = parseOk(R"({
        "games": [],
        "apps": [{
            "title": "RetroArch",
            "author": "libretro",
            "version": "1.19.1",
            "description": "Frontend multi-systèmes.",
            "url_nro": "apps/RetroArch/retroarch_switch.nro",
            "url_icon": "apps/RetroArch/icon.jpg",
            "size": 25165824,
            "crc32": "0A1B2C3D"
        }]
    })");
    ASSERT_EQ(index.apps.size(), 1u);
    const AppEntry& app = index.apps[0];
    EXPECT_EQ(app.title, "RetroArch");
    EXPECT_EQ(app.author, "libretro");
    EXPECT_EQ(app.version, "1.19.1");
    EXPECT_EQ(app.description, "Frontend multi-systèmes.");
    EXPECT_EQ(app.nroUrl, "ftp://nas.local/shop/apps/RetroArch/retroarch_switch.nro");
    EXPECT_EQ(app.iconUrl, "ftp://nas.local/shop/apps/RetroArch/icon.jpg");
    EXPECT_EQ(app.sizeBytes, 25165824u);
    EXPECT_EQ(app.crc32, "0a1b2c3d");
    EXPECT_EQ(app.folder, "RetroArch");  // from the title
    EXPECT_EQ(app.id, "app/RetroArch");
    EXPECT_TRUE(index.warnings.empty());
}

TEST(AppsIndex, EmulatorsIsAnAliasAndBothAreMerged) {
    RepoIndex index = parseOk(R"({
        "emulators": [{"title": "melonDS", "url_nro": "apps/melonds.nro"}],
        "apps": [{"title": "pNES", "url": "apps/pnes.nro", "folder": "pNES-Switch"}]
    })");
    ASSERT_EQ(index.apps.size(), 2u);
    EXPECT_EQ(index.apps[0].title, "pNES");  // "apps" first, then "emulators"
    EXPECT_EQ(index.apps[0].folder, "pNES-Switch");
    EXPECT_EQ(index.apps[0].nroUrl, "ftp://nas.local/shop/apps/pnes.nro");  // "url" accepted too
    EXPECT_EQ(index.apps[0].category, AppCategory::Homebrew);
    EXPECT_EQ(index.apps[1].title, "melonDS");
    EXPECT_EQ(index.apps[1].category, AppCategory::Emulator);
    EXPECT_TRUE(index.games.empty());  // an apps-only index is a valid shop
}

TEST(AppsIndex, BadEntriesAreSkippedWithAWarning) {
    RepoIndex index = parseOk(R"({
        "games": [],
        "apps": [
            {"url_nro": "apps/no-title.nro"},
            {"title": "No URL"},
            {"title": "Bad size", "url_nro": "a.nro", "size": -3},
            {"title": "Bad CRC", "url_nro": "a.nro", "crc32": "xyz"},
            {"title": "Bad version", "url_nro": "a.nro", "version": 2},
            {"title": "../../atmosphere", "url_nro": "evil.nro"},
            {"title": "Evil folder", "url_nro": "evil.nro", "folder": "../atmosphere"},
            {"title": "Twice", "url_nro": "a.nro"},
            {"title": "twice", "url_nro": "b.nro"},
            "not an object"
        ]
    })");
    ASSERT_EQ(index.apps.size(), 1u);
    EXPECT_EQ(index.apps[0].title, "Twice");
    EXPECT_EQ(index.warnings.size(), 9u);  // same folder on FAT = duplicate
}

TEST(AppsIndex, SectionsMustBeArrays) {
    auto result = RepoIndexParser().parse(R"({"apps": {}})");
    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.error().code, ErrorCode::ParseError);
    EXPECT_FALSE(RepoIndexParser().parse(R"({"name": "empty"})").ok());  // nothing to offer at all
}

TEST(AppsIndex, FolderNamesAreMadeFatSafe) {
    RepoIndex index = parseOk(R"({"apps": [{"title": "Moonlight: Switch?", "url_nro": "m.nro"}]})");
    ASSERT_EQ(index.apps.size(), 1u);
    EXPECT_EQ(index.apps[0].folder, "Moonlight_ Switch_");
}
