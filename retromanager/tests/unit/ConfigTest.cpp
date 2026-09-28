#include <gtest/gtest.h>

#include "MemoryFileSystem.hpp"
#include "retromanager/network/SourceFactory.hpp"
#include "retromanager/parsers/ConfigParser.hpp"
#include "retromanager/services/ConfigManager.hpp"

using namespace rm;

namespace {

AppConfig sample() {
    AppConfig config;
    config.shop.type = "ftp";
    config.shop.url = "ftp://nas.local:2121/shop/index.json";
    config.shop.username = "leo";
    config.shop.password = "p\"ss\\wörd";
    config.shop.verifyTls = true;
    config.savesUrl = "ftp://nas.local:2121/Saves/";
    config.sysclk.enabled = false;
    config.sysclk.titleId = "05B9D58000000000";
    return config;
}

const char* kPath = "/switch/RetroManager/config.json";

}  // namespace

// --- ConfigParser --------------------------------------------------------

TEST(ConfigParser, RoundTrips) {
    AppConfig config = sample();
    auto parsed = parseConfig(serializeConfig(config));
    ASSERT_TRUE(parsed.ok()) << parsed.error().describe();
    EXPECT_EQ(parsed.value(), config);
}

TEST(ConfigParser, SerializedFormIsReadable) {
    std::string text = serializeConfig(sample());
    EXPECT_NE(text.find("\"version\": 1"), std::string::npos) << text;
    EXPECT_NE(text.find("\"url\": \"ftp://nas.local:2121/shop/index.json\""), std::string::npos) << text;
    EXPECT_EQ(text.back(), '\n');
}

TEST(ConfigParser, MissingFieldsTakeDefaults) {
    auto parsed = parseConfig(R"({"shop": {"url": "ftp://nas/"}})");
    ASSERT_TRUE(parsed.ok());
    EXPECT_EQ(parsed.value().shop.type, "ftp");
    EXPECT_EQ(parsed.value().shop.url, "ftp://nas/");
    EXPECT_EQ(parsed.value().shop.username, "");
    EXPECT_FALSE(parsed.value().shop.verifyTls);

    auto empty = parseConfig("{}");
    ASSERT_TRUE(empty.ok());
    EXPECT_EQ(empty.value(), AppConfig{});
}

TEST(ConfigParser, ReadsSavesAndSysClkSettings) {
    auto parsed = parseConfig(R"({"saves_url": "ftp://nas/Saves/", "sysclk": {"title_id": "0100000000001000"}})");
    ASSERT_TRUE(parsed.ok()) << parsed.error().describe();
    EXPECT_EQ(parsed.value().savesUrl, "ftp://nas/Saves/");
    EXPECT_TRUE(parsed.value().sysclk.enabled);
    EXPECT_EQ(parsed.value().sysclk.titleId, "0100000000001000");

    AppConfig defaults;
    EXPECT_EQ(defaults.sysclk.titleId, "010000000000100D");  // Album: RetroArch .nro run from hbmenu
    EXPECT_TRUE(defaults.savesUrl.empty());

    EXPECT_EQ(parseConfig(R"({"saves_url": 3})").error().code, ErrorCode::ParseError);
    EXPECT_EQ(parseConfig(R"({"sysclk": {"title_id": "05B9"}})").error().code, ErrorCode::ParseError);
    EXPECT_EQ(parseConfig(R"({"sysclk": {"enabled": "yes"}})").error().code, ErrorCode::ParseError);
}

TEST(ConfigParser, IgnoresUnknownFields) {
    EXPECT_TRUE(parseConfig(R"({"future": true, "shop": {"type": "mock", "color": "red"}})").ok());
}

TEST(ConfigParser, RejectsInvalidDocuments) {
    EXPECT_EQ(parseConfig("").error().code, ErrorCode::ParseError);
    EXPECT_EQ(parseConfig("{\"shop\": ").error().code, ErrorCode::ParseError);
    EXPECT_EQ(parseConfig("[]").error().code, ErrorCode::ParseError);
    EXPECT_EQ(parseConfig(R"({"shop": "ftp://nas"})").error().code, ErrorCode::ParseError);
    EXPECT_EQ(parseConfig(R"({"shop": {"password": 1234}})").error().code, ErrorCode::ParseError);
    EXPECT_EQ(parseConfig(R"({"shop": {"verifyTls": "no"}})").error().code, ErrorCode::ParseError);
    EXPECT_EQ(parseConfig(R"({"shop": {"type": "smb"}})").error().code, ErrorCode::ParseError);
    EXPECT_EQ(parseConfig(R"({"version": 2})").error().code, ErrorCode::Unsupported);
}

// --- ConfigManager -------------------------------------------------------

TEST(ConfigManager, CreatesDefaultsWhenMissing) {
    test::MemoryFileSystem fs;
    ConfigManager manager(fs, kPath);

    auto config = manager.loadOrCreate();
    ASSERT_TRUE(config.ok()) << config.error().describe();
    EXPECT_EQ(config.value(), AppConfig{});
    ASSERT_TRUE(fs.isFile(kPath));  // template written for the user to edit
    EXPECT_EQ(parseConfig(fs.readFile(kPath).value()).value(), AppConfig{});
}

TEST(ConfigManager, LoadsExistingFile) {
    test::MemoryFileSystem fs;
    ASSERT_TRUE(fs.createDirectories("/switch/RetroManager").ok());
    ASSERT_TRUE(fs.writeFile(kPath, serializeConfig(sample())).ok());

    auto config = ConfigManager(fs, kPath).loadOrCreate();
    ASSERT_TRUE(config.ok());
    EXPECT_EQ(config.value(), sample());
}

TEST(ConfigManager, InvalidFileIsReportedAndLeftUntouched) {
    test::MemoryFileSystem fs;
    ASSERT_TRUE(fs.createDirectories("/switch/RetroManager").ok());
    const std::string broken = "{\"shop\": {\"url\": \"ftp://nas\", }";  // user typo
    ASSERT_TRUE(fs.writeFile(kPath, broken).ok());

    auto config = ConfigManager(fs, kPath).loadOrCreate();
    ASSERT_FALSE(config.ok());
    EXPECT_EQ(config.error().code, ErrorCode::ParseError);
    EXPECT_NE(config.error().message.find(kPath), std::string::npos) << config.error().message;
    EXPECT_EQ(fs.readFile(kPath).value(), broken);
}

TEST(ConfigManager, SaveRoundTrips) {
    test::MemoryFileSystem fs;
    ConfigManager manager(fs, kPath);
    ASSERT_TRUE(manager.save(sample()).ok());
    EXPECT_EQ(manager.loadOrCreate().value(), sample());
}

TEST(ConfigManager, ReadOnlyCardReportsTheError) {
    test::MemoryFileSystem fs;
    fs.setReadOnly(true);
    auto config = ConfigManager(fs, kPath).loadOrCreate();
    ASSERT_FALSE(config.ok());
    EXPECT_EQ(config.error().code, ErrorCode::PermissionDenied);
}

// --- SourceFactory -------------------------------------------------------

TEST(SourceFactory, MapsAnFtpUrl) {
    ShopConfig shop = sample().shop;
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
    ShopConfig ftp = sample().shop;
    EXPECT_EQ(createRemoteSource(ftp)->describe(), "ftp://leo@nas.local:2121/shop/index.json");

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
    demo.shop.type = "mock";
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
