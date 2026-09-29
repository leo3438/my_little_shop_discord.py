#include <gtest/gtest.h>

#include "retromanager/network/HttpClient.hpp"

using namespace rm;

namespace {

HttpConfig config(std::string url = "https://shop.example.org/retro/shop.json") {
    HttpConfig c;
    c.indexUrl = std::move(url);
    c.username = "leo";
    c.password = "secret";
    return c;
}

}  // namespace

TEST(HttpClient, ValidatesItsConfiguration) {
    EXPECT_TRUE(HttpClient::validate(config()).ok());
    EXPECT_TRUE(HttpClient::validate(config("http://192.168.1.20:8080/shop.json")).ok());
    EXPECT_FALSE(HttpClient::validate(config("")).ok());
    EXPECT_FALSE(HttpClient::validate(config("ftp://nas/shop.json")).ok());
    EXPECT_FALSE(HttpClient::validate(config("not a url")).ok());
}

TEST(HttpClient, DescribesItselfWithoutThePassword) {
    HttpClient client(config());
    EXPECT_EQ(client.describe(), "https://leo@shop.example.org/retro/shop.json");
    EXPECT_EQ(client.describe().find("secret"), std::string::npos);
    EXPECT_EQ(client.indexUrl(), "https://shop.example.org/retro/shop.json");
}

TEST(HttpClient, SendsCredentialsOnlyToItsOwnServer) {
    HttpClient client(config());
    EXPECT_TRUE(client.sendsCredentialsTo("https://shop.example.org/retro/roms/a.sfc"));
    EXPECT_TRUE(client.sendsCredentialsTo("https://SHOP.example.org:443/other"));
    EXPECT_FALSE(client.sendsCredentialsTo("http://shop.example.org/retro/a.sfc"));   // other scheme (clear text)
    EXPECT_FALSE(client.sendsCredentialsTo("https://shop.example.org:8443/a.sfc"));   // other port
    EXPECT_FALSE(client.sendsCredentialsTo("https://thumbnails.libretro.com/a.png"));  // other host
    EXPECT_FALSE(client.sendsCredentialsTo("garbage"));
}

TEST(HttpClient, MapsHttpStatusesToErrors) {
    EXPECT_FALSE(HttpClient::errorForStatus(200, 0).has_value());
    EXPECT_FALSE(HttpClient::errorForStatus(206, 100).has_value());
    EXPECT_EQ(HttpClient::errorForStatus(200, 100)->code, ErrorCode::Unsupported);  // Range ignored: no resume
    EXPECT_EQ(HttpClient::errorForStatus(206, 0)->code, ErrorCode::NetworkError);   // partial answer to a full request
    EXPECT_EQ(HttpClient::errorForStatus(416, 100)->code, ErrorCode::Unsupported);
    EXPECT_EQ(HttpClient::errorForStatus(401, 0)->code, ErrorCode::AuthenticationFailed);
    EXPECT_EQ(HttpClient::errorForStatus(403, 0)->code, ErrorCode::PermissionDenied);
    EXPECT_EQ(HttpClient::errorForStatus(404, 0)->code, ErrorCode::NotFound);
    EXPECT_EQ(HttpClient::errorForStatus(410, 0)->code, ErrorCode::NotFound);
    EXPECT_EQ(HttpClient::errorForStatus(500, 0)->code, ErrorCode::NetworkError);
    EXPECT_EQ(HttpClient::errorForStatus(503, 0)->code, ErrorCode::NetworkError);
}

TEST(HttpClient, HasNoDirectoryListingNorUpload) {
    HttpClient client(config());
    EXPECT_EQ(client.listDirectory("https://shop.example.org/").error().code, ErrorCode::Unsupported);
    CancellationToken cancel;
    auto reader = [](char*, std::size_t) -> Result<std::size_t> { return std::size_t{0}; };
    EXPECT_EQ(client.uploadFile("https://shop.example.org/a", reader, 0, nullptr, cancel).error().code,
              ErrorCode::Unsupported);
}
