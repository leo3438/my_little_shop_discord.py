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

}

TEST(ConfigParser, IgnoresUnknownFields) {
    EXPECT_TRUE(parseConfig(R"({"future": true, "shop": {"type": "mock", "color": "red"}})").ok());
}

TEST(ConfigParser, RejectsWhatIsNotJson) {
    EXPECT_EQ(parseConfig("").error().code, ErrorCode::ParseError);
    EXPECT_EQ(parseConfig("{\"shop\": ").error().code, ErrorCode::ParseError);
    EXPECT_EQ(parseConfig("[]").error().code, ErrorCode::ParseError);
    EXPECT_EQ(parseConfig(R"({"version": 2})").error().code, ErrorCode::Unsupported);
}

TEST(ConfigParser, SyntaxErrorsGiveTheLine) {
    auto parsed = parseConfig("{\n  \"sources\": [\n    {\"url\": \"ftp://nas/\" \"user\": \"leo\"}\n  ]\n}\n");
    ASSERT_FALSE(parsed.ok());
    EXPECT_NE(parsed.error().message.find("line 3"), std::string::npos) << parsed.error().message;
}

// config.json is edited by hand, often in Notepad: accept what people write.
TEST(ConfigParser, HandWrittenFilesAreAccepted) {
    auto parsed = parseConfig("\xEF\xBB\xBF"  // UTF-8 BOM (Windows Notepad)
                              R"({
        // my sources
        "sources": [
            {
                "name": "NAS du salon",
                "url": "ftp://192.168.1.20/shop/",   /* the Synology */
                "user": "leo",
                "pass": 1234,
            },
            {"name": "Web", "index_url": "https://retro.example.org/shop.json", "verify_tls": "false"},
            "ftp://192.168.1.21/games/",
        ],
        "active_source": "nas du salon",
    })");
    ASSERT_TRUE(parsed.ok()) << parsed.error().describe();
    const AppConfig& config = parsed.value();
    ASSERT_EQ(config.sources.size(), 3u);
    EXPECT_EQ(config.sources[0].name, "NAS du salon");
    EXPECT_EQ(config.sources[0].type, "ftp");
    EXPECT_EQ(config.sources[0].url, "ftp://192.168.1.20/shop/");
    EXPECT_EQ(config.sources[0].username, "leo");
    EXPECT_EQ(config.sources[0].password, "1234");
    EXPECT_EQ(config.sources[1].type, "http");
    EXPECT_EQ(config.sources[1].url, "https://retro.example.org/shop.json");
    EXPECT_FALSE(config.sources[1].verifyTls);
    EXPECT_EQ(config.sources[2].url, "ftp://192.168.1.21/games/");
    EXPECT_EQ(config.sources[2].name, "Source 3");
    EXPECT_EQ(config.activeSource, "NAS du salon");
    EXPECT_TRUE(config.warnings.empty()) << config.warnings.front();
}

TEST(ConfigParser, CommentMarkersInsideStringsAreKept) {
    auto parsed = parseConfig(R"({"sources": [{"url": "http://example.org/a//b/*c*/,]", "password": "x,}"}]})");
    ASSERT_TRUE(parsed.ok()) << parsed.error().describe();
    EXPECT_EQ(parsed.value().sources[0].url, "http://example.org/a//b/*c*/,]");
    EXPECT_EQ(parsed.value().sources[0].password, "x,}");
}

TEST(ConfigParser, TypesAreCaseInsensitiveWithAliases) {
    auto parsed = parseConfig(R"({"sources": [{"name": "a", "type": "FTP", "url": "ftp://a/"},
                                               {"name": "b", "type": "HTTPS", "url": "https://b/"},
                                               {"name": "c", "type": "ftps", "url": "ftps://c/"},
                                               {"name": "d", "type": "Web", "url": "https://d/"}]})");
    ASSERT_TRUE(parsed.ok()) << parsed.error().describe();
    EXPECT_EQ(parsed.value().sources[0].type, "ftp");
    EXPECT_EQ(parsed.value().sources[1].type, "http");
    EXPECT_EQ(parsed.value().sources[2].type, "ftp");
    EXPECT_EQ(parsed.value().sources[3].type, "http");
}

TEST(ConfigParser, ABadSourceIsSkippedNotTheWholeFile) {
    auto parsed = parseConfig(R"({"sources": [
        {"name": "Bon", "url": "ftp://nas/shop/"},
        {"name": "Mauvais", "type": "smb", "url": "smb://nas/"},
        3,
        {"name": "Mot de passe", "url": "ftp://nas2/", "password": ["oops"]}
    ]})");
    ASSERT_TRUE(parsed.ok()) << parsed.error().describe();
    ASSERT_EQ(parsed.value().sources.size(), 1u);
    EXPECT_EQ(parsed.value().sources[0].name, "Bon");
    // One warning per skipped entry, naming it: the Sources screen shows them.
    ASSERT_EQ(parsed.value().warnings.size(), 3u);
    EXPECT_NE(parsed.value().warnings[0].find("Mauvais"), std::string::npos) << parsed.value().warnings[0];
    EXPECT_NE(parsed.value().warnings[0].find("smb"), std::string::npos);
    EXPECT_NE(parsed.value().warnings[1].find("sources[2]"), std::string::npos) << parsed.value().warnings[1];
    EXPECT_NE(parsed.value().warnings[2].find("password"), std::string::npos) << parsed.value().warnings[2];
}

TEST(ConfigParser, DuplicateNamesAreRenamed) {
    auto parsed = parseConfig(R"({"sources": [{"name": "NAS", "url": "ftp://a/"}, {"name": "nas", "url": "ftp://b/"}]})");
    ASSERT_TRUE(parsed.ok());
    ASSERT_EQ(parsed.value().sources.size(), 2u);
    EXPECT_EQ(parsed.value().sources[1].name, "nas (2)");
    EXPECT_EQ(parsed.value().sources[1].url, "ftp://b/");
    EXPECT_EQ(parsed.value().warnings.size(), 1u);
}

TEST(ConfigParser, WrongTopLevelFieldsFallBackWithAWarning) {
    auto parsed = parseConfig(R"({"saves_url": 3, "sysclk": {"title_id": "05B9", "enabled": "yes"},
                                  "scraper": {"enabled": "non"}, "active_source": 1,
                                  "sources": {"name": "Seule", "url": "ftp://nas/"}})");
    ASSERT_TRUE(parsed.ok()) << parsed.error().describe();
    const AppConfig& config = parsed.value();
    EXPECT_EQ(config.savesUrl, "");
    EXPECT_EQ(config.sysclk.titleId, AppConfig{}.sysclk.titleId);
    EXPECT_TRUE(config.sysclk.enabled);   // "yes"
    EXPECT_FALSE(config.scraper.enabled); // "non"
    ASSERT_EQ(config.sources.size(), 1u); // a single object is one source
    EXPECT_EQ(config.sources[0].name, "Seule");
    EXPECT_EQ(config.activeSource, "Seule");
    EXPECT_EQ(config.warnings.size(), 3u);  // saves_url, sysclk.title_id, active_source
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

TEST(ConfigManager, RewritingKeepsTheHandWrittenFileAsBackup) {
    test::MemoryFileSystem fs;
    ASSERT_TRUE(fs.createDirectories("/switch/RetroManager").ok());
    const std::string mine = "{ // mine\n \"sources\": [\"ftp://nas/shop/\"] }\n";
    ASSERT_TRUE(fs.writeFile(kPath, mine).ok());
    ConfigManager manager(fs, kPath);
    auto config = manager.loadOrCreate();
    ASSERT_TRUE(config.ok()) << config.error().describe();
    ASSERT_TRUE(manager.save(config.value()).ok());
    EXPECT_EQ(fs.readFile(std::string(kPath) + ".bak").value(), mine);
    EXPECT_EQ(manager.loadOrCreate().value(), config.value());
    // Saving the same content again leaves the backup alone.
    ASSERT_TRUE(manager.save(config.value()).ok());
    EXPECT_EQ(fs.readFile(std::string(kPath) + ".bak").value(), mine);
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

