#include <gtest/gtest.h>

#include "retromanager/parsers/CfgDocument.hpp"

using namespace rm;

namespace {

// A deliberately messy file: hand-written comments, odd spacing, tabs,
// unquoted and empty values, duplicates, a commented-out key, an include,
// a key that is a prefix of another.
const std::string kMessy =
    "## RetroArch config, edited by hand\n"
    "# rgui_browser_directory = \"/commented/out\"\n"
    "\n"
    "video_vsync = \"true\"\n"
    "rgui_browser_directory   =   \"default\"   \n"
    "rgui_browser_directory_extra = \"keep me\"\n"
    "\tmenu_driver=\"ozone\"\n"
    "audio_latency = 64\n"
    "empty_value = \"\"\n"
    "#include \"/retroarch/overrides.cfg\"\n"
    "savefile_directory = \"/retroarch/saves\"\n"
    "video_vsync = \"false\"\n"
    "this line is not an assignment\n"
    "last_key = \"x\"";  // no final newline

}  // namespace

// --- lossless round trip -------------------------------------------------

TEST(CfgDocument, UnmodifiedDocumentIsReproducedByteForByte) {
    for (const std::string& text :
         {kMessy, std::string(""), std::string("\n\n"), std::string("a = \"1\"\r\nb = \"2\"\r\n"),
          std::string("\xEF\xBB\xBFkey = \"v\"\n"), std::string("   \n# only a comment"), std::string("x=\"y\"")}) {
        CfgDocument doc = CfgDocument::parse(text);
        EXPECT_EQ(doc.serialize(), text);
        EXPECT_FALSE(doc.modified());
    }
}

// --- reading -------------------------------------------------------------

TEST(CfgDocument, ReadsQuotedUnquotedAndEmptyValues) {
    CfgDocument doc = CfgDocument::parse(kMessy);
    EXPECT_EQ(doc.get("rgui_browser_directory"), "default");
    EXPECT_EQ(doc.get("menu_driver"), "ozone");
    EXPECT_EQ(doc.get("audio_latency"), "64");
    EXPECT_EQ(doc.get("empty_value"), "");
    EXPECT_EQ(doc.get("last_key"), "x");
    EXPECT_EQ(doc.get("video_vsync"), "true");  // first assignment
    EXPECT_FALSE(doc.get("missing").has_value());
    EXPECT_FALSE(doc.get("rgui_browser").has_value());  // no prefix matching
}

TEST(CfgDocument, CommentsAndIncludesAreNotKeys) {
    CfgDocument doc = CfgDocument::parse(kMessy);
    for (const std::string& key : doc.keys()) {
        EXPECT_NE(key.front(), '#') << key;
    }
    EXPECT_EQ(doc.keys().size(), 9u);  // video_vsync counted twice
}

// --- editing -------------------------------------------------------------

TEST(CfgDocument, SetRewritesOnlyTheValue) {
    CfgDocument doc = CfgDocument::parse(kMessy);
    ASSERT_TRUE(doc.set("rgui_browser_directory", "/roms/nds/").ok());
    EXPECT_TRUE(doc.modified());

    std::string expected = kMessy;
    expected.replace(expected.find("\"default\""), 9, "\"/roms/nds/\"");
    EXPECT_EQ(doc.serialize(), expected);  // spacing, comments, other keys untouched
    EXPECT_EQ(doc.get("rgui_browser_directory"), "/roms/nds/");
    EXPECT_EQ(doc.get("rgui_browser_directory_extra"), "keep me");
}

TEST(CfgDocument, SetUpdatesEveryDuplicate) {
    CfgDocument doc = CfgDocument::parse(kMessy);
    ASSERT_TRUE(doc.set("video_vsync", "true").ok());
    std::string out = doc.serialize();
    EXPECT_EQ(out.find("video_vsync = \"false\""), std::string::npos);
    std::size_t first = out.find("video_vsync = \"true\"");
    ASSERT_NE(first, std::string::npos);
    EXPECT_NE(out.find("video_vsync = \"true\"", first + 1), std::string::npos);
}

TEST(CfgDocument, SetKeepsTheQuotingStyle) {
    CfgDocument doc = CfgDocument::parse("audio_latency = 64\nname = \"a\"\n");
    ASSERT_TRUE(doc.set("audio_latency", "128").ok());
    ASSERT_TRUE(doc.set("name", "b c").ok());
    EXPECT_EQ(doc.serialize(), "audio_latency = 128\nname = \"b c\"\n");

    // An unquoted value that would need quotes gets them.
    ASSERT_TRUE(doc.set("audio_latency", "two words").ok());
    EXPECT_EQ(doc.serialize(), "audio_latency = \"two words\"\nname = \"b c\"\n");
}

TEST(CfgDocument, SettingTheSameValueIsNotAModification) {
    CfgDocument doc = CfgDocument::parse(kMessy);
    ASSERT_TRUE(doc.set("menu_driver", "ozone").ok());
    EXPECT_FALSE(doc.modified());
    EXPECT_EQ(doc.serialize(), kMessy);
}

TEST(CfgDocument, CommentedOutKeyIsNeverUncommented) {
    CfgDocument doc = CfgDocument::parse("# rgui_browser_directory = \"/old\"\n");
    ASSERT_TRUE(doc.set("rgui_browser_directory", "/roms/nds/").ok());
    EXPECT_EQ(doc.serialize(), "# rgui_browser_directory = \"/old\"\nrgui_browser_directory = \"/roms/nds/\"\n");
}

TEST(CfgDocument, AppendHandlesMissingFinalNewlineAndCrlf) {
    CfgDocument noNewline = CfgDocument::parse("a = \"1\"");
    ASSERT_TRUE(noNewline.set("b", "2").ok());
    EXPECT_EQ(noNewline.serialize(), "a = \"1\"\nb = \"2\"\n");

    CfgDocument crlf = CfgDocument::parse("a = \"1\"\r\n");
    ASSERT_TRUE(crlf.set("b", "2").ok());
    EXPECT_EQ(crlf.serialize(), "a = \"1\"\r\nb = \"2\"\r\n");

    CfgDocument empty = CfgDocument::parse("");
    ASSERT_TRUE(empty.set("b", "2").ok());
    EXPECT_EQ(empty.serialize(), "b = \"2\"\n");
}

TEST(CfgDocument, CrlfLinesStayCrlfWhenEdited) {
    CfgDocument doc = CfgDocument::parse("a = \"1\"\r\nb = \"2\"\r\n");
    ASSERT_TRUE(doc.set("a", "9").ok());
    EXPECT_EQ(doc.serialize(), "a = \"9\"\r\nb = \"2\"\r\n");
}

TEST(CfgDocument, BomIsPreserved) {
    CfgDocument doc = CfgDocument::parse("\xEF\xBB\xBF" "a = \"1\"\n");
    EXPECT_EQ(doc.get("a"), "1");  // the BOM is not part of the first key
    ASSERT_TRUE(doc.set("a", "2").ok());
    EXPECT_EQ(doc.serialize(), "\xEF\xBB\xBF" "a = \"2\"\n");
}

TEST(CfgDocument, ValuesMayContainHashAndEquals) {
    CfgDocument doc = CfgDocument::parse("cheat0_code = \"02000000+1=2#x\"\n");
    EXPECT_EQ(doc.get("cheat0_code"), "02000000+1=2#x");
}

TEST(CfgDocument, RejectsUnrepresentableKeysAndValues) {
    CfgDocument doc = CfgDocument::parse(kMessy);
    EXPECT_EQ(doc.set("", "x").error().code, ErrorCode::InvalidArgument);
    EXPECT_EQ(doc.set("bad key", "x").error().code, ErrorCode::InvalidArgument);
    EXPECT_EQ(doc.set("# comment", "x").error().code, ErrorCode::InvalidArgument);
    EXPECT_EQ(doc.set("k", "has \"quote\"").error().code, ErrorCode::InvalidArgument);
    EXPECT_EQ(doc.set("k", "two\nlines").error().code, ErrorCode::InvalidArgument);
    EXPECT_FALSE(doc.modified());
    EXPECT_EQ(doc.serialize(), kMessy);
}

TEST(CfgDocument, RemoveDropsEveryAssignment) {
    CfgDocument doc = CfgDocument::parse(kMessy);
    EXPECT_TRUE(doc.remove("video_vsync"));
    EXPECT_FALSE(doc.contains("video_vsync"));
    EXPECT_FALSE(doc.remove("video_vsync"));
    EXPECT_EQ(doc.serialize().find("video_vsync"), std::string::npos);
    EXPECT_NE(doc.serialize().find("## RetroArch config, edited by hand\n"), std::string::npos);
}

TEST(CfgDocument, ParsesACheatFile) {
    CfgDocument cht = CfgDocument::parse(
        "cheats = 2\n\n"
        "cheat0_desc = \"Infinite Health\"\ncheat0_code = \"021C0000+0000FFFF\"\ncheat0_enable = false\n\n"
        "cheat1_desc = \"Max Money\"\ncheat1_code = \"021C1000+0098967F\"\ncheat1_enable = false\n");
    EXPECT_EQ(cht.get("cheats"), "2");
    EXPECT_EQ(cht.get("cheat1_desc"), "Max Money");
    EXPECT_EQ(cht.get("cheat0_enable"), "false");
}

TEST(CfgDocument, RandomDocumentsRoundTripAndStayEditable) {
    // Property test over messy inputs built from the characters that matter.
    const std::string alphabet = "ab_=\" \t#\r\n\xEF\xBB\xBF";
    std::uint32_t seed = 12345;
    auto next = [&seed] { return seed = seed * 1103515245u + 12345u; };

    for (int round = 0; round < 3000; ++round) {
        std::string text;
        std::size_t length = next() % 60;
        for (std::size_t i = 0; i < length; ++i) text += alphabet[(next() >> 8) % alphabet.size()];

        CfgDocument doc = CfgDocument::parse(text);
        ASSERT_EQ(doc.serialize(), text) << "round " << round;

        ASSERT_TRUE(doc.set("ab", "new value").ok());
        CfgDocument reparsed = CfgDocument::parse(doc.serialize());
        ASSERT_EQ(reparsed.get("ab"), "new value") << "round " << round;
    }
}
