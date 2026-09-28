#include <gtest/gtest.h>

#include "MemoryFileSystem.hpp"
#include "MockSdCard.hpp"
#include "retromanager/core/AppContext.hpp"

using namespace rm;

TEST(AppContext, InitializeCreatesAppDirectories) {
    auto fs = std::make_shared<test::MemoryFileSystem>();
    AppContext context(fs, SdLayout{});

    ASSERT_TRUE(context.initialize().ok());
    for (const std::string& dir : context.layout().appDirectories()) {
        EXPECT_TRUE(fs->isDirectory(dir)) << dir;
    }
}

TEST(AppContext, InitializeIsIdempotentAndPreservesData) {
    std::shared_ptr<test::MemoryFileSystem> fs = test::makeMockSdCard();
    AppContext context(fs, SdLayout{});

    ASSERT_TRUE(context.initialize().ok());
    ASSERT_TRUE(fs->writeFile("/switch/RetroManager/config.json", "{}").ok());
    ASSERT_TRUE(context.initialize().ok());
    EXPECT_EQ(fs->readFile("/switch/RetroManager/config.json").value(), "{}");
}

TEST(AppContext, InitializeDoesNotCreateRetroArchDirectories) {
    auto fs = std::make_shared<test::MemoryFileSystem>();
    AppContext context(fs, SdLayout{});

    ASSERT_TRUE(context.initialize().ok());
    EXPECT_FALSE(context.isRetroArchInstalled());
    EXPECT_FALSE(fs->exists("/retroarch"));
}

TEST(AppContext, DetectsRetroArchOnTheMockSdCard) {
    AppContext context(test::makeMockSdCard(), SdLayout{});
    EXPECT_TRUE(context.isRetroArchInstalled());
}

TEST(AppContext, InitializeReportsWriteProtectedCard) {
    auto fs = std::make_shared<test::MemoryFileSystem>();
    fs->setReadOnly(true);
    AppContext context(fs, SdLayout{});

    Status status = context.initialize();
    ASSERT_FALSE(status.ok());
    EXPECT_EQ(status.error().code, ErrorCode::PermissionDenied);
}
