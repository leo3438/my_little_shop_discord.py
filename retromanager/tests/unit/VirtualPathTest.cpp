#include <gtest/gtest.h>

#include "retromanager/platform/VirtualPath.hpp"

using namespace rm;

namespace {

std::string normalized(std::string_view path) {
    auto result = vpath::normalize(path);
    EXPECT_TRUE(result.ok()) << path << " -> " << result.error().describe();
    return result.ok() ? result.value() : std::string();
}

ErrorCode normalizeError(std::string_view path) {
    auto result = vpath::normalize(path);
    EXPECT_FALSE(result.ok()) << path << " should have been rejected";
    return result.ok() ? ErrorCode::IoError : result.error().code;
}

}  // namespace

TEST(VirtualPath, NormalizeKeepsCanonicalPaths) {
    EXPECT_EQ(normalized("/"), "/");
    EXPECT_EQ(normalized("/retroarch/retroarch.cfg"), "/retroarch/retroarch.cfg");
}

TEST(VirtualPath, NormalizeCollapsesSeparatorsAndDots) {
    EXPECT_EQ(normalized("//retroarch///cores/"), "/retroarch/cores");
    EXPECT_EQ(normalized("/retroarch/./cores/."), "/retroarch/cores");
    EXPECT_EQ(normalized("/roms/snes/../gba/game.gba"), "/roms/gba/game.gba");
    EXPECT_EQ(normalized("/a/b/../.."), "/");
}

TEST(VirtualPath, NormalizeConvertsBackslashes) {
    EXPECT_EQ(normalized("\\roms\\snes\\zelda.sfc"), "/roms/snes/zelda.sfc");
}

TEST(VirtualPath, NormalizeRejectsInvalidPaths) {
    EXPECT_EQ(normalizeError(""), ErrorCode::InvalidPath);
    EXPECT_EQ(normalizeError("retroarch/retroarch.cfg"), ErrorCode::InvalidPath);
    EXPECT_EQ(normalizeError("sdmc:/retroarch"), ErrorCode::InvalidPath);
    EXPECT_EQ(normalizeError(std::string_view("/a\0b", 4)), ErrorCode::InvalidPath);
}

TEST(VirtualPath, NormalizeRejectsRootEscape) {
    EXPECT_EQ(normalizeError("/.."), ErrorCode::InvalidPath);
    EXPECT_EQ(normalizeError("/roms/../../etc/passwd"), ErrorCode::InvalidPath);
}

TEST(VirtualPath, JoinAppendsAndNormalizes) {
    EXPECT_EQ(vpath::join("/retroarch", "cores").value(), "/retroarch/cores");
    EXPECT_EQ(vpath::join("/", "roms/snes").value(), "/roms/snes");
    EXPECT_EQ(vpath::join("/roms/snes", "../gba").value(), "/roms/gba");
    EXPECT_FALSE(vpath::join("/roms", "../../x").ok());
}

TEST(VirtualPath, Decomposition) {
    EXPECT_EQ(vpath::parent("/roms/snes/zelda.sfc"), "/roms/snes");
    EXPECT_EQ(vpath::parent("/roms"), "/");
    EXPECT_EQ(vpath::parent("/"), "/");

    EXPECT_EQ(vpath::filename("/roms/snes/zelda.sfc"), "zelda.sfc");
    EXPECT_EQ(vpath::filename("/"), "");

    EXPECT_EQ(vpath::stem("/roms/snes/zelda.sfc"), "zelda");
    EXPECT_EQ(vpath::stem("/roms/archive.tar.gz"), "archive.tar");
    EXPECT_EQ(vpath::stem("/retroarch/.hidden"), ".hidden");

    EXPECT_EQ(vpath::extension("/roms/snes/zelda.sfc"), ".sfc");
    EXPECT_EQ(vpath::extension("/roms/README"), "");
    EXPECT_EQ(vpath::extension("/retroarch/.hidden"), "");
}

TEST(VirtualPath, IsWithin) {
    EXPECT_TRUE(vpath::isWithin("/retroarch/cores", "/retroarch"));
    EXPECT_TRUE(vpath::isWithin("/retroarch", "/retroarch"));
    EXPECT_TRUE(vpath::isWithin("/anything", "/"));
    EXPECT_FALSE(vpath::isWithin("/retroarch2", "/retroarch"));
    EXPECT_FALSE(vpath::isWithin("/roms", "/retroarch"));
}
