// End to end: FtpClient against a real FTP server serving
// tests/fixtures/ftp_root (see tools/test_ftp_server.py).
//
// Skipped unless RM_TEST_FTP_PORT is set, so a plain `ctest` needs no
// server. The CI starts one and sets the variable.

#include <gtest/gtest.h>

#include <cstdlib>

#include "retromanager/core/ITaskRunner.hpp"
#include "retromanager/network/FtpClient.hpp"
#include "retromanager/services/ShopService.hpp"

using namespace rm;

namespace {

class FtpIntegration : public ::testing::Test {
  protected:
    void SetUp() override {
        const char* port = std::getenv("RM_TEST_FTP_PORT");
        if (port == nullptr || *port == '\0') GTEST_SKIP() << "RM_TEST_FTP_PORT not set (see tools/test_ftp_server.py)";
        config.host = "127.0.0.1";
        config.port = static_cast<std::uint16_t>(std::atoi(port));
        config.username = "retro";
        config.password = "manager";
        config.indexPath = "/shop/index.json";
        config.connectTimeoutSeconds = 5;
        config.transferTimeoutSeconds = 10;
    }

    FtpConfig config;
};

}  // namespace

TEST_F(FtpIntegration, LoadsAndParsesTheShopIndex) {
    FtpClient client(config);
    ImmediateTaskRunner tasks;
    ShopService shop(client, tasks);

    auto index = shop.loadIndex();
    ASSERT_TRUE(index.ok()) << index.error().describe();
    EXPECT_EQ(index.value().name, "Test FTP shop");
    ASSERT_EQ(index.value().games.size(), 2u);

    const std::string base = "ftp://127.0.0.1:" + std::to_string(config.port) + "/shop/";
    EXPECT_EQ(index.value().games[0].romUrl, base + "roms/snes/Super%20Mario%20World%20(USA).sfc");
    EXPECT_EQ(index.value().games[1].fileName, "Pokémon Émeraude (France).gba");
}

TEST_F(FtpIntegration, DownloadsFilesWithSpacesAndUnicode) {
    FtpClient client(config);
    auto snes = client.fetchFile("/shop/roms/snes/Super Mario World (USA).sfc", 1024);
    ASSERT_TRUE(snes.ok()) << snes.error().describe();
    EXPECT_EQ(snes.value(), "MOCK ROM snes\n");

    auto gba = client.fetchFile("/shop/roms/gba/Pokémon Émeraude (France).gba", 1024);
    ASSERT_TRUE(gba.ok()) << gba.error().describe();
    EXPECT_EQ(gba.value(), "MOCK ROM gba\n");
}

TEST_F(FtpIntegration, WrongPasswordIsAuthenticationFailed) {
    config.password = "wrong";
    auto result = FtpClient(config).fetchIndex();
    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.error().code, ErrorCode::AuthenticationFailed) << result.error().describe();
}

TEST_F(FtpIntegration, MissingFileIsNotFound) {
    config.indexPath = "/shop/nope.json";
    auto result = FtpClient(config).fetchIndex();
    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.error().code, ErrorCode::NotFound) << result.error().describe();
}

TEST_F(FtpIntegration, OversizedIndexIsRejected) {
    config.maxIndexBytes = 16;
    auto result = FtpClient(config).fetchIndex();
    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.error().code, ErrorCode::IoError);
    EXPECT_NE(result.error().message.find("larger than 16 bytes"), std::string::npos);
}
