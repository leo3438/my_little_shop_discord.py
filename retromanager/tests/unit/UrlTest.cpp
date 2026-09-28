#include <gtest/gtest.h>

#include "retromanager/core/Url.hpp"

using namespace rm;

TEST(Url, HasScheme) {
    EXPECT_TRUE(url::hasScheme("ftp://nas/a"));
    EXPECT_TRUE(url::hasScheme("https://x"));
    EXPECT_TRUE(url::hasScheme("svn+ssh://x"));
    EXPECT_FALSE(url::hasScheme("roms/a.sfc"));
    EXPECT_FALSE(url::hasScheme("/roms/a.sfc"));
    EXPECT_FALSE(url::hasScheme("://x"));
    EXPECT_FALSE(url::hasScheme("1ftp://x"));
    EXPECT_FALSE(url::hasScheme("roms/a:b.sfc"));  // ':' after a '/' is not a scheme
}

TEST(Url, PercentDecode) {
    EXPECT_EQ(url::percentDecode("Super%20Mario%20World"), "Super Mario World");
    EXPECT_EQ(url::percentDecode("Pok%C3%A9mon"), "Pokémon");
    EXPECT_EQ(url::percentDecode("100%"), "100%");
    EXPECT_EQ(url::percentDecode("%zz%4"), "%zz%4");
    EXPECT_EQ(url::percentDecode("a+b"), "a+b");  // '+' is not a space in paths
}

TEST(Url, PercentEncodePath) {
    EXPECT_EQ(url::percentEncodePath("/roms/snes/Super Mario (USA).sfc"), "/roms/snes/Super%20Mario%20%28USA%29.sfc");
    EXPECT_EQ(url::percentEncodePath("/a#b?c"), "/a%23b%3Fc");
    EXPECT_EQ(url::percentEncodePath("Pokémon"), "Pok%C3%A9mon");
    EXPECT_EQ(url::percentDecode(url::percentEncodePath("/x y/é#")), "/x y/é#");
}

TEST(Url, ResolveAbsoluteReferenceIsUntouched) {
    EXPECT_EQ(url::resolve("ftp://nas/shop/index.json", "https://cdn/a.png").value(), "https://cdn/a.png");
    EXPECT_EQ(url::resolve("", "ftp://nas/a.sfc").value(), "ftp://nas/a.sfc");
}

TEST(Url, ResolveRelativeReferences) {
    const std::string base = "ftp://leo@nas.local:2121/shop/index.json";
    EXPECT_EQ(url::resolve(base, "roms/a.sfc").value(), "ftp://leo@nas.local:2121/shop/roms/a.sfc");
    EXPECT_EQ(url::resolve(base, "./roms/a.sfc").value(), "ftp://leo@nas.local:2121/shop/roms/a.sfc");
    EXPECT_EQ(url::resolve(base, "../art/a.png").value(), "ftp://leo@nas.local:2121/art/a.png");
    EXPECT_EQ(url::resolve(base, "../../../a").value(), "ftp://leo@nas.local:2121/a");
    EXPECT_EQ(url::resolve(base, "/roms/b.sfc").value(), "ftp://leo@nas.local:2121/roms/b.sfc");
    EXPECT_EQ(url::resolve(base, "//other/x.sfc").value(), "ftp://other/x.sfc");
    EXPECT_EQ(url::resolve(base, "roms/a.sfc#Name.sfc").value(), "ftp://leo@nas.local:2121/shop/roms/a.sfc#Name.sfc");
    EXPECT_EQ(url::resolve(base, "dl?id=1").value(), "ftp://leo@nas.local:2121/shop/dl?id=1");
    EXPECT_EQ(url::resolve("https://h", "a.sfc").value(), "https://h/a.sfc");
    EXPECT_EQ(url::resolve("https://h/dir/", "sub/").value(), "https://h/dir/sub/");
}

TEST(Url, ResolveRelativeWithoutBaseFails) {
    auto result = url::resolve("", "roms/a.sfc");
    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.error().code, ErrorCode::InvalidArgument);
    EXPECT_FALSE(url::resolve("not a url", "a.sfc").ok());
}

TEST(Url, FragmentHelpers) {
    EXPECT_EQ(url::fragment("https://n/dl?id=1#Game.gba"), "Game.gba");
    EXPECT_EQ(url::fragment("https://n/a"), "");
    EXPECT_EQ(url::stripFragment("https://n/dl?id=1#Game.gba"), "https://n/dl?id=1");
}

TEST(Url, LastPathSegment) {
    EXPECT_EQ(url::lastPathSegment("ftp://nas/roms/Super%20Mario.sfc?x=1#f"), "Super Mario.sfc");
    EXPECT_EQ(url::lastPathSegment("ftp://nas/roms/"), "");
    EXPECT_EQ(url::lastPathSegment("ftp://nas"), "");
    EXPECT_EQ(url::lastPathSegment("relative/a.gba"), "a.gba");
}
