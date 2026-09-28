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

TEST(LocalFileSystem, StagingFileIsRemovedAfterCommitAndAbort) {
    test::TempDir dir;
    LocalFileSystem fs(dir.path());
    const auto staging = dir.path() / (std::string("save.srm") + LocalFileSystem::kPartialSuffix);

    {
        auto stream = fs.openWrite("/save.srm");
        ASSERT_TRUE(stream.ok());
        ASSERT_TRUE(stream.value()->write("abc", 3).ok());
        EXPECT_TRUE(std::filesystem::exists(staging));
    }
    EXPECT_FALSE(std::filesystem::exists(staging));

    ASSERT_TRUE(fs.writeFile("/save.srm", "abc").ok());
    EXPECT_FALSE(std::filesystem::exists(staging));
}

TEST(LocalFileSystem, ReadsTheFixtureSdCardInPlace) {
    LocalFileSystem fs(test::fixtureSdCardDir());
    EXPECT_TRUE(fs.isFile("/retroarch/retroarch.cfg"));
    EXPECT_TRUE(fs.isDirectory("/roms/snes"));
}
