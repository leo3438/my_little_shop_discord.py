#include <gtest/gtest.h>

#include "MemoryFileSystem.hpp"
#include "retromanager/parsers/ConfigParser.hpp"
#include "retromanager/services/ConfigManager.hpp"

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
    EXPECT_EQ(parsed.value().activeShop().type, "ftp");
    EXPECT_EQ(parsed.value().activeShop().url, "ftp://nas/");
    EXPECT_EQ(parsed.value().activeShop().username, "");
    EXPECT_FALSE(parsed.value().activeShop().verifyTls);

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

TEST(ConfigParser, ReadsSeveralSources) {
    auto parsed = parseConfig(R"({
        "sources": [
            {"name": "NAS", "url": "ftp://nas.local/shop/", "username": "leo", "password": "pw"},
            {"name": "Web", "url": "https://retro.example.org/shop.json"},
            {"url": "http://192.168.1.30:8080/index.json", "verifyTls": false},
            {"name": "Démo", "type": "mock"}
        ],
        "active_source": "web"
    })");
    ASSERT_TRUE(parsed.ok()) << parsed.error().describe();
    const AppConfig& config = parsed.value();
    ASSERT_EQ(config.sources.size(), 4u);
    EXPECT_EQ(config.sources[0].type, "ftp");  // from the URL
    EXPECT_FALSE(config.sources[0].verifyTls);  // home NAS: off by default
    EXPECT_EQ(config.sources[1].type, "http");
    EXPECT_TRUE(config.sources[1].verifyTls);   // the web: on by default
    EXPECT_EQ(config.sources[2].name, "Source 3");
    EXPECT_FALSE(config.sources[2].verifyTls);
    EXPECT_EQ(config.sources[3].type, "mock");
    EXPECT_EQ(config.activeSource, "Web");  // names match ignoring case
    EXPECT_EQ(config.activeShop().url, "https://retro.example.org/shop.json");
}

TEST(ConfigParser, TheOldSingleShopBecomesTheFirstSource) {
    auto parsed = parseConfig(R"({"shop": {"url": "ftp://nas.local/shop/", "username": "leo"}})");
    ASSERT_TRUE(parsed.ok());
    ASSERT_EQ(parsed.value().sources.size(), 1u);
    EXPECT_EQ(parsed.value().sources[0].name, "NAS");
    EXPECT_EQ(parsed.value().activeSource, "NAS");
    // Written back in the new format.
    std::string text = serializeConfig(parsed.value());
    EXPECT_NE(text.find("\"sources\""), std::string::npos) << text;
    EXPECT_EQ(text.find("\"shop\""), std::string::npos) << text;
}

TEST(ConfigParser, AnUnknownActiveSourceFallsBackToTheFirst) {
    auto parsed = parseConfig(R"({"sources": [{"name": "A", "url": "ftp://a/"}, {"name": "B", "url": "ftp://b/"}],
                                  "active_source": "deleted"})");
    ASSERT_TRUE(parsed.ok());
    EXPECT_EQ(parsed.value().activeSource, "A");
    auto none = parseConfig(R"({"sources": []})");
    ASSERT_TRUE(none.ok());
    EXPECT_TRUE(none.value().sources.empty());
    EXPECT_EQ(none.value().active(), nullptr);
    EXPECT_EQ(none.value().activeShop().url, "");
}

TEST(ConfigParser, RejectsInvalidSources) {
    EXPECT_EQ(parseConfig(R"({"sources": {}})").error().code, ErrorCode::ParseError);
    EXPECT_EQ(parseConfig(R"({"sources": [3]})").error().code, ErrorCode::ParseError);
    EXPECT_EQ(parseConfig(R"({"sources": [{"name": "A"}, {"name": "a"}]})").error().code, ErrorCode::ParseError);
    EXPECT_EQ(parseConfig(R"({"sources": [{"type": "smb"}]})").error().code, ErrorCode::ParseError);
    EXPECT_EQ(parseConfig(R"({"active_source": 1})").error().code, ErrorCode::ParseError);
    EXPECT_EQ(parseConfig(R"({"scraper": {"enabled": "yes"}})").error().code, ErrorCode::ParseError);
}

TEST(ConfigParser, ScraperDefaults) {
    AppConfig defaults;
    EXPECT_TRUE(defaults.scraper.enabled);
    EXPECT_EQ(defaults.scraper.baseUrl, "https://thumbnails.libretro.com/");
    auto parsed = parseConfig(R"({"scraper": {"base_url": "http://127.0.0.1:8121/thumbnails/"}})");
    ASSERT_TRUE(parsed.ok());
    EXPECT_EQ(parsed.value().scraper.baseUrl, "http://127.0.0.1:8121/thumbnails/");
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

