#include <gtest/gtest.h>

#include <set>

#include "retromanager/core/Format.hpp"
#include "retromanager/models/Systems.hpp"

using namespace rm;

TEST(Systems, FindIsCaseInsensitive) {
    ASSERT_NE(systems::find("SNES"), nullptr);
    EXPECT_EQ(systems::find("snes")->displayName, "Super Nintendo");
    EXPECT_EQ(systems::find("dreamcast"), nullptr);
}

TEST(Systems, InfersSystemFromExtension) {
    EXPECT_EQ(systems::fromFileName("Super Mario World (USA).sfc"), "snes");
    EXPECT_EQ(systems::fromFileName("ZELDA.SMC"), "snes");
    EXPECT_EQ(systems::fromFileName("Pokemon Platine.nds"), "nds");
    EXPECT_EQ(systems::fromFileName("Sonic.md"), "megadrive");
    EXPECT_EQ(systems::fromFileName("game.zip"), "");  // ambiguous
    EXPECT_EQ(systems::fromFileName("README"), "");
    EXPECT_EQ(systems::fromFileName(".sfc"), "");
}

TEST(Systems, ExtensionsAreUniqueAcrossSystems) {
    std::set<std::string> seen;
    for (const SystemInfo& system : systems::all()) {
        for (const std::string& extension : system.extensions) {
            EXPECT_TRUE(seen.insert(extension).second) << extension << " is claimed twice";
        }
    }
}

TEST(Systems, DisplayNameFallsBackToId) {
    EXPECT_EQ(systems::displayName("gba"), "Game Boy Advance");
    EXPECT_EQ(systems::displayName("unknown"), "unknown");
}

TEST(Format, Bytes) {
    EXPECT_EQ(formatBytes(0), "0 B");
    EXPECT_EQ(formatBytes(1023), "1023 B");
    EXPECT_EQ(formatBytes(1024), "1 KB");
    EXPECT_EQ(formatBytes(1536), "1.5 KB");
    EXPECT_EQ(formatBytes(524288), "512 KB");
    EXPECT_EQ(formatBytes(134217728), "128 MB");
    EXPECT_EQ(formatBytes(3221225472ull), "3 GB");
    EXPECT_EQ(formatBytes(5905580032ull), "5.5 GB");
}
