#include <gtest/gtest.h>

#include "retromanager/network/SourceFactory.hpp"

using namespace rm;

namespace {

AppConfig sample() {
    AppConfig config;
    ShopConfig& nas = config.sources.at(0);
    nas.name = "NAS";
    nas.type = "ftp";
    nas.url = "ftp://nas.local:2121/shop/index.json";
    nas.username = "leo";
    nas.password = "p\"ss\\wörd";
    nas.verifyTls = true;
    ShopConfig web;
    web.name = "Boutique web";
    web.type = "http";
    web.url = "https://retro.example.org/shop.json";
    web.verifyTls = true;
    config.sources.push_back(web);
    config.activeSource = "Boutique web";
    config.caBundle = "sdmc:/switch/RetroManager/cacert.pem";
    config.scraper.enabled = false;
    config.savesUrl = "ftp://nas.local:2121/Saves/";
    config.sysclk.enabled = false;
    config.sysclk.titleId = "05B9D58000000000";
    return config;
}

}  // namespace

// --- SourceFactory -------------------------------------------------------

TEST(SourceFactory, MapsAnFtpUrl) {
    ShopConfig shop = sample().sources[0];
    auto ftp = ftpConfigFromShop(shop);
    ASSERT_TRUE(ftp.ok()) << ftp.error().describe();
    EXPECT_EQ(ftp.value().host, "nas.local");
    EXPECT_EQ(ftp.value().port, 2121);
    EXPECT_EQ(ftp.value().indexPath, "/shop/index.json");
    EXPECT_EQ(ftp.value().username, "leo");
    EXPECT_EQ(ftp.value().password, shop.password);
    EXPECT_FALSE(ftp.value().useTls);
    EXPECT_TRUE(ftp.value().verifyPeer);
}

TEST(SourceFactory, AppliesDefaults) {
    ShopConfig shop;
    shop.url = "ftps://nas.local/Mes%20Jeux/";
    auto ftp = ftpConfigFromShop(shop);
    ASSERT_TRUE(ftp.ok());
    EXPECT_EQ(ftp.value().port, 21);
    EXPECT_EQ(ftp.value().indexPath, "/Mes Jeux/index.json");  // decoded; re-encoded by FtpClient
    EXPECT_EQ(ftp.value().username, "anonymous");
    EXPECT_TRUE(ftp.value().useTls);
    EXPECT_FALSE(ftp.value().verifyPeer);  // verifyTls defaults to off
}

TEST(SourceFactory, TakesCredentialsFromTheUrlWhenFieldsAreEmpty) {
    ShopConfig shop;
    shop.url = "ftp://leo:s%40cret@nas.local/index.json";
    auto ftp = ftpConfigFromShop(shop);
    ASSERT_TRUE(ftp.ok());
    EXPECT_EQ(ftp.value().username, "leo");
    EXPECT_EQ(ftp.value().password, "s@cret");
}

TEST(SourceFactory, RejectsUnusableUrls) {
    ShopConfig shop;
    EXPECT_EQ(ftpConfigFromShop(shop).error().code, ErrorCode::NotConfigured);

    shop.url = "https://nas.local/index.json";
    EXPECT_EQ(ftpConfigFromShop(shop).error().code, ErrorCode::Unsupported);

    shop.url = "nas.local/index.json";
    EXPECT_EQ(ftpConfigFromShop(shop).error().code, ErrorCode::InvalidArgument);
}

TEST(SourceFactory, BuildsTheConfiguredSource) {
    ShopConfig ftp = sample().sources[0];
    EXPECT_EQ(createRemoteSource(ftp)->describe(), "ftp://leo@nas.local:2121/shop/index.json");
    ShopConfig web = sample().sources[1];
    EXPECT_EQ(createRemoteSource(web)->describe(), "https://retro.example.org/shop.json");

    ShopConfig mock;
    mock.type = "mock";
    auto demo = createRemoteSource(mock);
    EXPECT_TRUE(demo->fetchIndex().ok());
}

TEST(SourceFactory, SavesUseTheShopCredentialsOnlyOnTheSameServer) {
    AppConfig config = sample();  // shop on nas.local:2121, user leo
    config.savesUrl = "ftp://nas.local:2121/Saves";
    SavesSource same = createSavesSource(config);
    EXPECT_EQ(same.baseUrl, "ftp://nas.local:2121/Saves/");
    EXPECT_EQ(same.source->describe(), "ftp://leo@nas.local:2121/index.json");

    config.savesUrl = "ftp://backup.local/Saves/";  // another machine
    SavesSource other = createSavesSource(config);
    EXPECT_EQ(other.source->describe().find("leo"), std::string::npos);  // anonymous, password not sent
    EXPECT_EQ(other.baseUrl, "ftp://backup.local:21/Saves/");

    config.savesUrl = "ftp://saver:pw@backup.local/Saves/";  // explicit credentials win
    EXPECT_EQ(createSavesSource(config).source->describe(), "ftp://saver@backup.local:21/index.json");
    EXPECT_EQ(createSavesSource(config).baseUrl, "ftp://backup.local:21/Saves/");  // no password in the URL
}

TEST(SourceFactory, SavesSourceErrors) {
    AppConfig config = sample();
    config.savesUrl.clear();
    SavesSource none = createSavesSource(config);
    EXPECT_TRUE(none.baseUrl.empty());

    config.savesUrl = "https://nas/Saves/";
    SavesSource bad = createSavesSource(config);
    CancellationToken cancel;
    EXPECT_EQ(bad.source->listDirectory(bad.baseUrl).error().code, ErrorCode::NotConfigured);

    AppConfig demo;
    demo.sources[0].type = "mock";
    SavesSource mock = createSavesSource(demo);
    EXPECT_FALSE(mock.baseUrl.empty());
    EXPECT_TRUE(mock.source->listDirectory(mock.baseUrl).ok());
}

TEST(SourceFactory, UnconfiguredSourceReportsWhyEverywhere) {
    auto source = createRemoteSource(ShopConfig{});  // ftp with an empty URL
    auto index = source->fetchIndex();
    ASSERT_FALSE(index.ok());
    EXPECT_EQ(index.error().code, ErrorCode::NotConfigured);

    CancellationToken cancel;
    Status download = source->downloadFile(
        "ftp://x/a", [](const char*, std::size_t) { return success(); }, nullptr, cancel);
    ASSERT_FALSE(download.ok());
    EXPECT_EQ(download.error().code, ErrorCode::NotConfigured);
}
