#include <gtest/gtest.h>

#include "MockSdCard.hpp"
#include "retromanager/platform/SdLayout.hpp"

using namespace rm;

TEST(MockSdCard, ExposesTheStandardSwitchLayout) {
    auto sd = test::makeMockSdCard();
    SdLayout layout;

    for (const std::string& dir : {layout.retroarchDir, layout.coresDir, layout.systemDir, layout.savesDir,
                                   layout.statesDir, layout.cheatsDir, layout.coreConfigDir, layout.thumbnailsDir,
                                   layout.playlistsDir, layout.romsDir, std::string("/switch")}) {
        EXPECT_TRUE(sd->isDirectory(dir)) << dir;
    }
    EXPECT_TRUE(sd->isFile(layout.retroarchCfg));
    EXPECT_TRUE(sd->isFile(layout.sysClkConfig));
}

TEST(MockSdCard, ContainsSampleContent) {
    auto sd = test::makeMockSdCard();

    auto snes = sd->listDirectory("/roms/snes");
    ASSERT_TRUE(snes.ok());
    ASSERT_EQ(snes.value().size(), 2u);
    EXPECT_EQ(snes.value()[0].name, "Super Mario World (USA).sfc");

    EXPECT_TRUE(sd->isFile("/retroarch/cores/snes9x_libretro_libnx.nro"));
    EXPECT_TRUE(sd->isFile("/retroarch/saves/Super Mario World (USA).srm"));

    auto cfg = sd->readFile("/retroarch/retroarch.cfg");
    ASSERT_TRUE(cfg.ok());
    EXPECT_NE(cfg.value().find("savefile_directory = \"/retroarch/saves\""), std::string::npos);
}

TEST(MockSdCard, SkipsGitkeepPlaceholders) {
    auto sd = test::makeMockSdCard();
    auto cheats = sd->listDirectory("/retroarch/cheats");
    ASSERT_TRUE(cheats.ok());
    EXPECT_TRUE(cheats.value().empty());
}

TEST(MockSdCard, EachInstanceIsIndependent) {
    auto first = test::makeMockSdCard();
    auto second = test::makeMockSdCard();

    ASSERT_TRUE(first->removeAll("/roms").ok());
    EXPECT_FALSE(first->exists("/roms"));
    EXPECT_TRUE(second->isDirectory("/roms/snes"));
    EXPECT_TRUE(test::makeMockSdCard()->isDirectory("/roms/snes"));  // fixture on disk untouched
}

TEST(MockSdCard, ReadOnlyModeRejectsMutations) {
    auto sd = test::makeMockSdCard();
    sd->setReadOnly(true);

    EXPECT_EQ(sd->writeFile("/new.txt", "x").error().code, ErrorCode::PermissionDenied);
    EXPECT_EQ(sd->createDirectories("/new").error().code, ErrorCode::PermissionDenied);
    EXPECT_EQ(sd->remove("/retroarch/retroarch.cfg").error().code, ErrorCode::PermissionDenied);
    EXPECT_EQ(sd->removeAll("/roms").error().code, ErrorCode::PermissionDenied);
    EXPECT_EQ(sd->rename("/roms", "/roms2").error().code, ErrorCode::PermissionDenied);

    // reads still work
    EXPECT_TRUE(sd->readFile("/retroarch/retroarch.cfg").ok());
}
