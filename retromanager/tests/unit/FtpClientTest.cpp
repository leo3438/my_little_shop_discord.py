// FtpClient logic that needs no server.

#include <gtest/gtest.h>

#include "retromanager/network/FtpClient.hpp"

using namespace rm;

namespace {

FtpConfig config() {
    FtpConfig c;
    c.host = "nas.local";
    c.username = "leo";
    c.password = "s3cret";
    c.indexPath = "/shop/index.json";
    return c;
}

}  // namespace

TEST(FtpClient, BuildsEscapedUrls) {
    EXPECT_EQ(FtpClient::buildUrl(config(), "/shop/index.json").value(), "ftp://nas.local:21/shop/index.json");
    EXPECT_EQ(FtpClient::buildUrl(config(), "/roms/snes/Super Mario (USA).sfc").value(),
              "ftp://nas.local:21/roms/snes/Super%20Mario%20%28USA%29.sfc");
    EXPECT_EQ(FtpClient::buildUrl(config(), "/a#b?c").value(), "ftp://nas.local:21/a%23b%3Fc");

    FtpConfig v6 = config();
    v6.host = "fe80::1";
    v6.port = 2121;
    EXPECT_EQ(FtpClient::buildUrl(v6, "/x").value(), "ftp://[fe80::1]:2121/x");
}

TEST(FtpClient, UrlsAndDescriptionNeverContainThePassword) {
    FtpClient client(config());
    EXPECT_EQ(client.indexUrl(), "ftp://nas.local:21/shop/index.json");
    EXPECT_EQ(client.describe(), "ftp://leo@nas.local:21/shop/index.json");
    EXPECT_EQ(client.describe().find("s3cret"), std::string::npos);

    FtpConfig tls = config();
    tls.useTls = true;
    EXPECT_EQ(FtpClient(tls).describe(), "ftps://leo@nas.local:21/shop/index.json");
}

TEST(FtpClient, ValidatesConfiguration) {
    EXPECT_TRUE(FtpClient::validate(config()).ok());

    FtpConfig noHost = config();
    noHost.host = "";
    EXPECT_EQ(FtpClient::validate(noHost).error().code, ErrorCode::InvalidArgument);

    FtpConfig urlAsHost = config();
    urlAsHost.host = "ftp://nas.local";
    EXPECT_EQ(FtpClient::validate(urlAsHost).error().code, ErrorCode::InvalidArgument);

    FtpConfig noPort = config();
    noPort.port = 0;
    EXPECT_EQ(FtpClient::validate(noPort).error().code, ErrorCode::InvalidArgument);

    FtpConfig relative = config();
    relative.indexPath = "index.json";
    EXPECT_EQ(FtpClient::validate(relative).error().code, ErrorCode::InvalidArgument);
}

TEST(FtpClient, InvalidConfigurationFailsWithoutNetworkAccess) {
    FtpConfig bad = config();
    bad.host = "";
    auto result = FtpClient(bad).fetchIndex();
    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.error().code, ErrorCode::InvalidArgument);
    EXPECT_EQ(FtpClient(bad).indexUrl(), "");
}

TEST(FtpClient, ConnectionRefusedIsANetworkError) {
    FtpConfig closed = config();
    closed.host = "127.0.0.1";
    closed.port = 1;  // nothing listens there
    closed.connectTimeoutSeconds = 3;

    auto result = FtpClient(closed).fetchIndex();
    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.error().code, ErrorCode::NetworkError) << result.error().describe();
}
