#include <gtest/gtest.h>

#include "retromanager/network/MockRemoteSource.hpp"
#include "retromanager/network/SourceRouter.hpp"

using namespace rm;

namespace {

std::shared_ptr<MockRemoteSource> mock(const std::string& indexUrl, const std::string& name) {
    auto source = std::make_shared<MockRemoteSource>(R"({"name": ")" + name + R"(", "games": []})", indexUrl);
    return source;
}

Result<std::string> get(IRemoteSource& source, const std::string& url, std::uint64_t offset = 0) {
    std::string body;
    CancellationToken cancel;
    Status status = source.downloadFileFrom(
        url, offset, [&](const char* d, std::size_t n) { body.append(d, n); return success(); }, nullptr, cancel);
    if (!status) return status.error();
    return body;
}

struct Fixture {
    std::shared_ptr<MockRemoteSource> nas = mock("ftp://nas.local/shop/index.json", "NAS");
    std::shared_ptr<MockRemoteSource> web = mock("https://retro.example.org/shop.json", "Web");
    std::shared_ptr<MockRemoteSource> publicWeb = mock("https://public.invalid/", "public");
    SourceRouter router{publicWeb};

    Fixture() {
        nas->addFile("ftp://nas.local/shop/roms/a.sfc", "from the NAS");
        web->addFile("https://retro.example.org/roms/b.sfc", "from the web shop");
        publicWeb->addFile("https://thumbnails.libretro.com/x.png", "public image");
        router.add("NAS", "ftp", nas);
        router.add("Web", "http", web);
    }
};

}  // namespace

TEST(SourceRouter, TheShopScreenSeesTheActiveSource) {
    Fixture f;
    EXPECT_EQ(f.router.activeName(), "NAS");  // the first one added
    EXPECT_NE(f.router.fetchIndex().value().find("\"NAS\""), std::string::npos);
    EXPECT_EQ(f.router.indexUrl(), "ftp://nas.local/shop/index.json");

    ASSERT_TRUE(f.router.setActive("web"));  // ignoring case
    EXPECT_EQ(f.router.activeName(), "Web");
    EXPECT_NE(f.router.fetchIndex().value().find("\"Web\""), std::string::npos);
    EXPECT_EQ(f.router.describe(), f.web->describe());
    EXPECT_FALSE(f.router.setActive("nope"));
}

TEST(SourceRouter, DownloadsGoToTheServerThatOwnsTheUrl) {
    Fixture f;
    ASSERT_TRUE(f.router.setActive("Web"));
    // A queued NAS download still goes to the NAS (with its credentials),
    // whatever source the shop screen shows now.
    EXPECT_EQ(get(f.router, "ftp://nas.local/shop/roms/a.sfc").value(), "from the NAS");
    EXPECT_EQ(f.router.route("ftp://NAS.local:21/shop/roms/a.sfc").get(), f.nas.get());  // same server
    EXPECT_EQ(f.router.route("ftps://nas.local/x").get(), f.nas.get());                  // explicit TLS, same NAS
    EXPECT_NE(f.router.route("http://retro.example.org/roms/b.sfc").get(), f.web.get());  // clear text is another server
    EXPECT_EQ(get(f.router, "https://retro.example.org/roms/b.sfc").value(), "from the web shop");
    EXPECT_EQ(get(f.router, "https://retro.example.org/roms/b.sfc", 5).value(), "the web shop");  // resume routed too
}

TEST(SourceRouter, OtherWebAddressesUseTheAnonymousPublicClient) {
    Fixture f;
    EXPECT_EQ(get(f.router, "https://thumbnails.libretro.com/x.png").value(), "public image");
    // An unknown FTP server is never contacted.
    EXPECT_EQ(get(f.router, "ftp://evil.example/x").error().code, ErrorCode::PermissionDenied);
    SourceRouter noPublic;
    EXPECT_EQ(get(noPublic, "https://thumbnails.libretro.com/x.png").error().code, ErrorCode::PermissionDenied);
}

TEST(SourceRouter, ListsAndRemovesSources) {
    Fixture f;
    auto list = f.router.sources();
    ASSERT_EQ(list.size(), 2u);
    EXPECT_EQ(list[0].name, "NAS");
    EXPECT_TRUE(list[0].active);
    EXPECT_EQ(list[1].type, "http");
    EXPECT_FALSE(list[1].active);

    std::shared_ptr<IRemoteSource> inFlight = f.router.route("ftp://nas.local/shop/roms/a.sfc");
    ASSERT_TRUE(f.router.remove("NAS"));
    EXPECT_EQ(f.router.activeName(), "Web");  // the next one takes over
    EXPECT_EQ(get(f.router, "ftp://nas.local/shop/roms/a.sfc").error().code, ErrorCode::PermissionDenied);
    EXPECT_EQ(get(*inFlight, "ftp://nas.local/shop/roms/a.sfc").value(), "from the NAS");  // still alive for a running transfer
    EXPECT_FALSE(f.router.remove("NAS"));

    ASSERT_TRUE(f.router.remove("Web"));
    EXPECT_EQ(f.router.activeName(), "");
    EXPECT_EQ(f.router.fetchIndex().error().code, ErrorCode::NotConfigured);
}

TEST(SourceRouter, NamesAreUnique) {
    Fixture f;
    EXPECT_FALSE(f.router.add("nas", "ftp", mock("ftp://other/", "x")));
    EXPECT_EQ(f.router.sources().size(), 2u);
}

TEST(SourceRouter, ReplaceKeepsThePlaceAndTheActiveMark) {
    Fixture f;
    ASSERT_TRUE(f.router.setActive("NAS"));
    auto renamed = mock("ftp://nas2.local/shop/index.json", "NAS 2");
    EXPECT_TRUE(f.router.replace("nas", "Salon", "ftp", renamed));
    auto list = f.router.sources();
    ASSERT_EQ(list.size(), 2u);
    EXPECT_EQ(list[0].name, "Salon");  // same position
    EXPECT_TRUE(list[0].active);       // still the active one, under its new name
    EXPECT_EQ(f.router.activeName(), "Salon");
    EXPECT_EQ(f.router.sourceNamed("salon"), renamed);
    EXPECT_EQ(f.router.sourceNamed("NAS"), nullptr);
    EXPECT_FALSE(f.router.replace("missing", "X", "ftp", renamed));
    EXPECT_FALSE(f.router.replace("Salon", "web", "ftp", renamed));  // name taken by another source
}

TEST(SourceRouter, SmbUrlsGoToTheSourceOfTheirShare) {
    auto media = mock("smb://nas.local:445/Media/shop.json", "Media");
    auto games = mock("smb://nas.local:445/Games/index.json", "Games");
    media->addFile("smb://nas.local:445/Media/a.nds", "media");
    games->addFile("smb://nas.local:445/Games/b.nds", "games");
    SourceRouter router;
    router.add("Media", "smb", media);
    router.add("Games", "smb", games);
    EXPECT_EQ(get(router, "smb://nas.local:445/Media/a.nds").value(), "media");
    EXPECT_EQ(router.route("smb://NAS.local/games/b.nds"), games);  // default port, share names ignore case
    EXPECT_EQ(router.route("smb://nas.local/Media/roms ds/x.nds"), media);
    EXPECT_EQ(get(router, "smb://nas.local:445/Other/c.nds").error().code, ErrorCode::PermissionDenied);
}
