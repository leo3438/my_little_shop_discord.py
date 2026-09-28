#include <gtest/gtest.h>

#include "retromanager/parsers/IniDocument.hpp"

using namespace rm;

namespace {

// Shaped like a real sys-clk config.ini edited by hand.
const std::string kSysClk =
    "; sys-clk configuration\n"
    "; my own notes: keep the GPU low in handheld\n"
    "[values]\n"
    "temp_log_interval_ms=0\n"
    "freq_log_interval_ms=0\n"
    "\n"
    "; Zelda BOTW\n"
    "[01007EF00011E000]\n"
    "docked_cpu=1785\n"
    "handheld_gpu = 460\n"
    "\n"
    "# RetroArch, old values\n"
    "[05b9d58000000000]\n"
    "handheld_cpu=1224\n"
    "\n";

}  // namespace

TEST(IniDocument, UnmodifiedDocumentIsReproducedByteForByte) {
    for (const std::string& text : {kSysClk, std::string(""), std::string("[a]\r\nk=v\r\n"),
                                    std::string("\xEF\xBB\xBF[a]\nk=v"), std::string("no section=1\n[x]"),
                                    std::string("[broken\nk=v\n")}) {
        IniDocument doc = IniDocument::parse(text);
        EXPECT_EQ(doc.serialize(), text);
        EXPECT_FALSE(doc.modified());
    }
}

TEST(IniDocument, ReadsKeysPerSection) {
    IniDocument doc = IniDocument::parse(kSysClk);
    EXPECT_EQ(doc.get("values", "temp_log_interval_ms"), "0");
    EXPECT_EQ(doc.get("01007EF00011E000", "docked_cpu"), "1785");
    EXPECT_EQ(doc.get("01007EF00011E000", "handheld_gpu"), "460");  // spaces around '='
    EXPECT_EQ(doc.get("05B9D58000000000", "handheld_cpu"), "1224");  // section names ignore case
    EXPECT_FALSE(doc.get("values", "docked_cpu").has_value());      // no leaking across sections
    EXPECT_FALSE(doc.get("missing", "docked_cpu").has_value());
    EXPECT_EQ(doc.sections(), (std::vector<std::string>{"values", "01007EF00011E000", "05b9d58000000000"}));
}

TEST(IniDocument, UpdatesAValueInPlace) {
    IniDocument doc = IniDocument::parse(kSysClk);
    ASSERT_TRUE(doc.set("05B9D58000000000", "handheld_cpu", "1785").ok());
    std::string expected = kSysClk;
    expected.replace(expected.find("handheld_cpu=1224"), 17, "handheld_cpu=1785");
    EXPECT_EQ(doc.serialize(), expected);
}

TEST(IniDocument, InsertsAMissingKeyAtTheEndOfItsSection) {
    IniDocument doc = IniDocument::parse(kSysClk);
    ASSERT_TRUE(doc.set("05B9D58000000000", "docked_cpu", "1785").ok());
    std::string expected = kSysClk;
    expected.replace(expected.find("handheld_cpu=1224\n"), 18, "handheld_cpu=1224\ndocked_cpu=1785\n");
    EXPECT_EQ(doc.serialize(), expected);
}

TEST(IniDocument, InsertsIntoTheRightSectionEvenInTheMiddle) {
    IniDocument doc = IniDocument::parse(kSysClk);
    ASSERT_TRUE(doc.set("01007EF00011E000", "handheld_cpu", "1581").ok());
    std::string out = doc.serialize();
    EXPECT_NE(out.find("handheld_gpu = 460\nhandheld_cpu = 1581\n\n# RetroArch"), std::string::npos) << out;
    EXPECT_EQ(IniDocument::parse(out).get("01007EF00011E000", "handheld_cpu"), "1581");
}

TEST(IniDocument, AppendsANewSectionBlock) {
    IniDocument doc = IniDocument::parse("[values]\ntemp_log_interval_ms=0\n");
    ASSERT_TRUE(doc.set("05B9D58000000000", "handheld_cpu", "1785").ok());
    ASSERT_TRUE(doc.set("05B9D58000000000", "docked_cpu", "1785").ok());
    EXPECT_EQ(doc.serialize(),
              "[values]\ntemp_log_interval_ms=0\n\n[05B9D58000000000]\nhandheld_cpu=1785\ndocked_cpu=1785\n");
}

TEST(IniDocument, AppendKeepsCrlfAndHandlesMissingNewline) {
    IniDocument crlf = IniDocument::parse("[a]\r\nk=v\r\n");
    ASSERT_TRUE(crlf.set("b", "x", "1").ok());
    EXPECT_EQ(crlf.serialize(), "[a]\r\nk=v\r\n\r\n[b]\r\nx=1\r\n");

    IniDocument noNewline = IniDocument::parse("[a]\nk=v");
    ASSERT_TRUE(noNewline.set("a", "j", "2").ok());
    EXPECT_EQ(noNewline.serialize(), "[a]\nk=v\nj=2\n");

    IniDocument empty = IniDocument::parse("");
    ASSERT_TRUE(empty.set("s", "k", "v").ok());
    EXPECT_EQ(empty.serialize(), "[s]\nk=v\n");
}

TEST(IniDocument, ReusesTheFilesSeparatorStyle) {
    IniDocument spaced = IniDocument::parse("[a]\nk = v\n");
    ASSERT_TRUE(spaced.set("b", "x", "1").ok());
    EXPECT_EQ(spaced.serialize(), "[a]\nk = v\n\n[b]\nx = 1\n");
}

TEST(IniDocument, KeylessSectionGetsTheKeyAfterItsOwnCommentsOnly) {
    // The comment separated by a blank line describes the next section.
    IniDocument doc = IniDocument::parse("[a]\n\n; about b\n[b]\nk=1\n");
    ASSERT_TRUE(doc.set("a", "x", "2").ok());
    EXPECT_EQ(doc.serialize(), "[a]\nx=2\n\n; about b\n[b]\nk=1\n");
}

TEST(IniDocument, CommentedOutValuesStayComments) {
    IniDocument doc = IniDocument::parse("[s]\n;docked_cpu=1020\n");
    ASSERT_TRUE(doc.set("s", "docked_cpu", "1785").ok());
    EXPECT_EQ(doc.serialize(), "[s]\n;docked_cpu=1020\ndocked_cpu=1785\n");
}

TEST(IniDocument, SameValueIsNotAModification) {
    IniDocument doc = IniDocument::parse(kSysClk);
    ASSERT_TRUE(doc.set("01007EF00011E000", "docked_cpu", "1785").ok());
    EXPECT_FALSE(doc.modified());
}

TEST(IniDocument, RejectsWhatTheFormatCannotHold) {
    IniDocument doc = IniDocument::parse(kSysClk);
    EXPECT_FALSE(doc.set("", "k", "v").ok());
    EXPECT_FALSE(doc.set("s", "", "v").ok());
    EXPECT_FALSE(doc.set("s", "a=b", "v").ok());
    EXPECT_FALSE(doc.set("s", ";k", "v").ok());
    EXPECT_FALSE(doc.set("s", "k", "two\nlines").ok());
    EXPECT_FALSE(doc.set("s]", "k", "v").ok());
    EXPECT_FALSE(doc.modified());
}

TEST(IniDocument, RandomDocumentsRoundTripAndStayEditable) {
    const std::string alphabet = "ab=[] ;#\r\n\t";
    std::uint32_t seed = 777;
    auto next = [&seed] { return seed = seed * 1103515245u + 12345u; };
    for (int round = 0; round < 3000; ++round) {
        std::string text;
        std::size_t length = next() % 60;
        for (std::size_t i = 0; i < length; ++i) text += alphabet[(next() >> 8) % alphabet.size()];

        IniDocument doc = IniDocument::parse(text);
        ASSERT_EQ(doc.serialize(), text) << "round " << round;
        ASSERT_TRUE(doc.set("sec", "ab", "value").ok());
        ASSERT_EQ(IniDocument::parse(doc.serialize()).get("sec", "ab"), "value") << "round " << round;
    }
}
