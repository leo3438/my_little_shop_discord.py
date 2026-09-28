// LocalFileSystem specifics that the shared contract cannot express:
// how virtual paths map onto the host directory.

#include <gtest/gtest.h>

#include <fstream>

#include "MockSdCard.hpp"
#include "TempDir.hpp"
#include "retromanager/platform/LocalFileSystem.hpp"

using namespace rm;

TEST(LocalFileSystem, MapsVirtualPathsUnderTheRoot) {
    test::TempDir dir;
    LocalFileSystem fs(dir.path());

    ASSERT_TRUE(fs.createDirectories("/retroarch").ok());
    ASSERT_TRUE(fs.writeFile("/retroarch/retroarch.cfg", "x").ok());
    EXPECT_TRUE(std::filesystem::is_regular_file(dir.path() / "retroarch" / "retroarch.cfg"));
}

TEST(LocalFileSystem, NeverTouchesAnythingOutsideTheRoot) {
    test::TempDir outer;
    std::filesystem::create_directories(outer.path() / "sd");
    { std::ofstream(outer.path() / "secret.txt") << "host file"; }
    LocalFileSystem fs(outer.path() / "sd");

    EXPECT_FALSE(fs.readFile("/../secret.txt").ok());
    EXPECT_FALSE(fs.removeAll("/..").ok());
    EXPECT_TRUE(std::filesystem::exists(outer.path() / "secret.txt"));
}

TEST(LocalFileSystem, StagesWritesInAHiddenTmpFileNextToTheTarget) {
    test::TempDir dir;
    LocalFileSystem fs(dir.path());
    const auto staging = dir.path() / ".Game.nds.tmp";

    {
        auto stream = fs.openWrite("/Game.nds");
        ASSERT_TRUE(stream.ok());
        ASSERT_TRUE(stream.value()->write("abc", 3).ok());
        EXPECT_TRUE(std::filesystem::exists(staging));                  // data goes to .Game.nds.tmp
        EXPECT_FALSE(std::filesystem::exists(dir.path() / "Game.nds"));  // target untouched
    }
    EXPECT_FALSE(std::filesystem::exists(staging));  // abandoned: cleaned up

    ASSERT_TRUE(fs.writeFile("/Game.nds", "abc").ok());
    EXPECT_FALSE(std::filesystem::exists(staging));  // committed: renamed
    EXPECT_TRUE(std::filesystem::is_regular_file(dir.path() / "Game.nds"));
}

TEST(LocalFileSystem, StaleStagingFileFromACrashIsOverwritten) {
    test::TempDir dir;
    { std::ofstream(dir.path() / ".Game.nds.tmp") << "half-written garbage from a power cut"; }
    LocalFileSystem fs(dir.path());

    ASSERT_TRUE(fs.writeFile("/Game.nds", "good").ok());
    EXPECT_EQ(fs.readFile("/Game.nds").value(), "good");
    EXPECT_FALSE(std::filesystem::exists(dir.path() / ".Game.nds.tmp"));
}

TEST(StagingPath, Convention) {
    EXPECT_EQ(stagingPath("/roms/nds/Game.nds"), "/roms/nds/.Game.nds.tmp");
    EXPECT_EQ(stagingPath("/save.srm"), "/.save.srm.tmp");
    EXPECT_TRUE(isStagingName(".Game.nds.tmp"));
    EXPECT_FALSE(isStagingName("Game.nds.tmp"));
    EXPECT_FALSE(isStagingName(".hidden"));
    EXPECT_FALSE(isStagingName(".tmp"));
}

TEST(LocalFileSystem, ReadsTheFixtureSdCardInPlace) {
    LocalFileSystem fs(test::fixtureSdCardDir());
    EXPECT_TRUE(fs.isFile("/retroarch/retroarch.cfg"));
    EXPECT_TRUE(fs.isDirectory("/roms/snes"));
}
