#include <gtest/gtest.h>

#include "MemoryFileSystem.hpp"
#include "retromanager/network/SourceCatalog.hpp"
#include "retromanager/parsers/ConfigParser.hpp"

using namespace rm;

namespace {

const char* kPath = "/switch/RetroManager/config.json";

struct Fixture {
    test::MemoryFileSystem fs;
    ConfigManager manager{fs, kPath};
    AppConfig config;
    SourceRouter router;
    std::unique_ptr<SourceCatalog> catalog;

    Fixture() {
        config.sources[0].url = "ftp://nas.local/shop/";
        for (const ShopConfig& shop : config.sources) router.add(shop.name, shop.type, std::shared_ptr<IRemoteSource>(createRemoteSource(shop)));
        catalog = std::make_unique<SourceCatalog>(config, router, manager, true);
    }

    AppConfig saved() { return parseConfig(fs.readFile(kPath).value()).value(); }
};

ShopConfig web(std::string name = "Web", std::string url = "https://retro.example.org/shop.json") {
    ShopConfig shop;
    shop.name = std::move(name);
    shop.type = "";  // what the Sources screen sends: deduced from the URL
    shop.url = std::move(url);
    return shop;
}

}  // namespace

TEST(SourceCatalog, AddsASourceAndSavesIt) {
    Fixture f;
    ShopConfig shop = web();
    Status added = f.catalog->add(shop);
    ASSERT_TRUE(added.ok()) << added.error().describe();

    ASSERT_EQ(f.router.sources().size(), 2u);
    EXPECT_EQ(f.router.sources()[1].name, "Web");
    EXPECT_EQ(f.router.sources()[1].type, "http");
    AppConfig saved = f.saved();
    ASSERT_EQ(saved.sources.size(), 2u);
    EXPECT_EQ(saved.sources[1].type, "http");
    EXPECT_TRUE(saved.sources[1].verifyTls);  // web default
    EXPECT_EQ(f.config, saved);                // memory and file agree
}

TEST(SourceCatalog, RefusesBadSourcesAndSavesNothing) {
    Fixture f;
    EXPECT_EQ(f.catalog->add(web("nas")).error().code, ErrorCode::InvalidArgument);           // name taken (any case)
    EXPECT_EQ(f.catalog->add(web("")).error().code, ErrorCode::InvalidArgument);              // no name
    EXPECT_EQ(f.catalog->add(web("X", "nas.local/shop")).error().code, ErrorCode::InvalidArgument);  // not a URL
    EXPECT_EQ(f.catalog->add(web("X", "dav://nas/shop")).error().code, ErrorCode::Unsupported);
    EXPECT_FALSE(f.fs.exists(kPath));
    EXPECT_EQ(f.router.sources().size(), 1u);
}

TEST(SourceCatalog, ActivatesAndRemoves) {
    Fixture f;
    ASSERT_TRUE(f.catalog->add(web()).ok());
    ASSERT_TRUE(f.catalog->activate("web").ok());
    EXPECT_EQ(f.router.activeName(), "Web");
    EXPECT_EQ(f.saved().activeSource, "Web");
    EXPECT_EQ(f.catalog->activate("nope").error().code, ErrorCode::NotFound);

    ASSERT_TRUE(f.catalog->remove("Web").ok());
    EXPECT_EQ(f.router.activeName(), "NAS");
    EXPECT_EQ(f.saved().activeSource, "NAS");
    EXPECT_EQ(f.saved().sources.size(), 1u);
    EXPECT_EQ(f.catalog->remove("Web").error().code, ErrorCode::NotFound);
}

TEST(SourceCatalog, NeverOverwritesAConfigItCouldNotRead) {
    test::MemoryFileSystem fs;
    ConfigManager manager(fs, kPath);
    AppConfig config;
    SourceRouter router;
    SourceCatalog catalog(config, router, manager, /*configLoaded=*/false);
    EXPECT_EQ(catalog.add(web()).error().code, ErrorCode::PermissionDenied);
    EXPECT_FALSE(fs.exists(kPath));
}

TEST(SourceCatalog, TypedSpacesAreTrimmed) {
    Fixture f;
    ShopConfig shop = web("  Web  ", "  https://retro.example.org/shop.json \n");
    shop.username = " leo ";
    ASSERT_TRUE(f.catalog->add(shop).ok());
    AppConfig saved = f.saved();
    EXPECT_EQ(saved.sources[1].name, "Web");
    EXPECT_EQ(saved.sources[1].url, "https://retro.example.org/shop.json");
    EXPECT_EQ(saved.sources[1].username, "leo");
}

TEST(SourceCatalog, TypeShownWhileTyping) {
    EXPECT_EQ(SourceCatalog::typeForUrl("ftp://nas/shop/"), "ftp");
    EXPECT_EQ(SourceCatalog::typeForUrl("FTPS://nas/"), "ftp");
    EXPECT_EQ(SourceCatalog::typeForUrl("https://site/shop.json"), "http");
    EXPECT_EQ(SourceCatalog::typeForUrl(" http://192.168.1.2:8080/"), "http");
    EXPECT_EQ(SourceCatalog::typeForUrl("smb://nas/"), "smb");
    EXPECT_EQ(SourceCatalog::typeForUrl("dav://nas/"), "");
    EXPECT_EQ(SourceCatalog::typeForUrl("nas.local/shop"), "");
    EXPECT_EQ(SourceCatalog::typeForUrl(""), "");
}

TEST(SourceCatalog, UpdatesASourceInPlace) {
    Fixture f;
    ASSERT_TRUE(f.catalog->add(web()).ok());
    ASSERT_TRUE(f.catalog->activate("Web").ok());

    ShopConfig edited = f.config.sources[1];
    edited.name = "Boutique";
    edited.url = "https://other.example.org/index.json";
    edited.username = "leo";
    edited.password = "pw";
    Status updated = f.catalog->update("Web", edited);
    ASSERT_TRUE(updated.ok()) << updated.error().describe();

    AppConfig saved = f.saved();
    ASSERT_EQ(saved.sources.size(), 2u);  // replaced, not added
    EXPECT_EQ(saved.sources[1].name, "Boutique");
    EXPECT_EQ(saved.sources[1].url, "https://other.example.org/index.json");
    EXPECT_EQ(saved.sources[1].username, "leo");
    EXPECT_EQ(saved.sources[1].password, "pw");
    EXPECT_EQ(saved.activeSource, "Boutique");  // the active source followed its rename
    ASSERT_EQ(f.router.sources().size(), 2u);
    EXPECT_EQ(f.router.sources()[1].name, "Boutique");
    EXPECT_NE(f.router.sources()[1].description.find("other.example.org"), std::string::npos);
}

TEST(SourceCatalog, UpdateChangesTheTypeWithTheUrl) {
    Fixture f;
    ShopConfig nas = f.config.sources[0];
    nas.type = "";  // what the form sends: deduced again from the URL
    nas.url = "https://retro.example.org/shop.json";
    ASSERT_TRUE(f.catalog->update(nas.name, nas).ok());
    EXPECT_EQ(f.saved().sources[0].type, "http");
    EXPECT_TRUE(f.saved().sources[0].verifyTls);
}

TEST(SourceCatalog, RefusedUpdatesChangeNothing) {
    Fixture f;
    ASSERT_TRUE(f.catalog->add(web()).ok());
    AppConfig before = f.saved();
    ShopConfig clash = f.config.sources[1];
    clash.name = "nas";  // another source's name
    EXPECT_EQ(f.catalog->update("Web", clash).error().code, ErrorCode::InvalidArgument);
    ShopConfig bad = f.config.sources[1];
    bad.url = "not a url";
    EXPECT_FALSE(f.catalog->update("Web", bad).ok());
    EXPECT_EQ(f.catalog->update("Missing", f.config.sources[1]).error().code, ErrorCode::NotFound);
    EXPECT_EQ(f.saved(), before);
    EXPECT_EQ(f.router.sources()[1].name, "Web");
    // Keeping its own name is fine (only another source's name clashes).
    ShopConfig same = f.config.sources[1];
    same.username = "x";
    EXPECT_TRUE(f.catalog->update("WEB", same).ok());
}

TEST(SourceCatalog, SmbSourcesAreDetectedAndChecked) {
    EXPECT_EQ(SourceCatalog::typeForUrl("smb://192.168.1.102/HDD-Storage1/"), "smb");
    EXPECT_EQ(SourceCatalog::typeForUrl("SMB://nas/Share/"), "smb");
    Fixture f;
    ShopConfig zima = web("ZimaOS", "smb://192.168.1.102/HDD-Storage1/roms ds/shop.json");
    zima.username = "leo";
    zima.password = "pw";
    Status added = f.catalog->add(zima);
    ASSERT_TRUE(added.ok()) << added.error().describe();
    EXPECT_EQ(f.saved().sources[1].type, "smb");
    EXPECT_EQ(f.router.sources()[1].type, "smb");
    EXPECT_NE(f.router.sources()[1].description.find("smb://leo@192.168.1.102:445/HDD-Storage1/roms ds/shop.json"),
              std::string::npos)
        << f.router.sources()[1].description;
    // A share is required.
    EXPECT_EQ(f.catalog->add(web("Bad", "smb://192.168.1.102/")).error().code, ErrorCode::InvalidArgument);
}
