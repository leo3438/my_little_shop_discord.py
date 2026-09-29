#include <gtest/gtest.h>

#include <cstring>

#include "retromanager/forwarder/nsp/Metadata.hpp"

using namespace rm;
using namespace rm::nsp;

namespace {

// Fake test keys: patterns, never real console keys.
const std::string kFakeKeys =
    "; test keys\n"
    "master_key_00 = 00112233445566778899aabbccddeeff\n"
    "HEADER_KEY = 000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f\n"
    "   key_area_key_application_00=A0A1A2A3A4A5A6A7A8A9AAABACADAEAF   \n"
    "key_area_key_application_01 = b0b1b2b3b4b5b6b7b8b9babbbcbdbebf\n";

std::uint32_t u32(const Bytes& b, std::size_t at) {
    return b[at] | (b[at + 1] << 8) | (b[at + 2] << 16) | (static_cast<std::uint32_t>(b[at + 3]) << 24);
}
std::uint64_t u64(const Bytes& b, std::size_t at) { return u32(b, at) | (static_cast<std::uint64_t>(u32(b, at + 4)) << 32); }
void put32(Bytes& b, std::size_t at, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) b[at + i] = static_cast<std::uint8_t>(v >> (8 * i));
}

// A minimal NPDM: META header, ACID at 0x80 (0x240 bytes), ACI0 after it.
Bytes fakeNpdm() {
    Bytes npdm(0x80 + 0x240 + 0x40, 0);
    std::memcpy(npdm.data(), "META", 4);
    put32(npdm, 0x78, 0x80);
    put32(npdm, 0x7C, 0x240);
    put32(npdm, 0x70, 0x80 + 0x240);
    put32(npdm, 0x74, 0x40);
    std::memcpy(npdm.data() + 0x80 + 0x200, "ACID", 4);
    std::memcpy(npdm.data() + 0x80 + 0x240, "ACI0", 4);
    return npdm;
}

}  // namespace

TEST(ProdKeysTest, ReadsTheTwoNeededKeys) {
    auto keys = parseProdKeys(kFakeKeys);
    ASSERT_TRUE(keys.ok()) << keys.error().describe();
    EXPECT_EQ(keys.value().headerDataKey[0], 0x00);
    EXPECT_EQ(keys.value().headerDataKey[15], 0x0f);
    EXPECT_EQ(keys.value().headerTweakKey[0], 0x10);
    EXPECT_EQ(keys.value().headerTweakKey[15], 0x1f);
    EXPECT_EQ(keys.value().keyAreaKey[0], 0xa0);
    EXPECT_EQ(keys.value().keyAreaKey[15], 0xaf);
}

TEST(ProdKeysTest, MissingKeyIsNamedWithoutLeakingValues) {
    auto keys = parseProdKeys("header_key = 000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f\n");
    ASSERT_FALSE(keys.ok());
    EXPECT_EQ(keys.error().code, ErrorCode::NotFound);
    EXPECT_NE(keys.error().message.find("key_area_key_application_00"), std::string::npos);
    EXPECT_EQ(keys.error().message.find("000102"), std::string::npos);

    auto none = parseProdKeys("");
    ASSERT_FALSE(none.ok());
    EXPECT_NE(none.error().message.find("header_key"), std::string::npos);
}

TEST(ProdKeysTest, WrongLengthOrBadHexIsAParseError) {
    auto shortKey = parseProdKeys("header_key = 0011\nkey_area_key_application_00 = a0a1a2a3a4a5a6a7a8a9aaabacadaeaf\n");
    ASSERT_FALSE(shortKey.ok());
    EXPECT_EQ(shortKey.error().code, ErrorCode::ParseError);
    EXPECT_EQ(shortKey.error().message.find("0011"), std::string::npos);
    auto badHex = parseProdKeys(
        "header_key = zz0102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f\n"
        "key_area_key_application_00 = a0a1a2a3a4a5a6a7a8a9aaabacadaeaf\n");
    EXPECT_EQ(badHex.error().code, ErrorCode::ParseError);
}

TEST(NacpTest, TitlesForEveryLanguageAndIds) {
    NacpSpec spec;
    spec.titleId = 0x0512345678901000ull;
    spec.name = "Pokémon Émeraude";
    spec.publisher = "RetroManager";
    spec.displayVersion = "1.2.3";
    Bytes nacp = buildNacp(spec);
    ASSERT_EQ(nacp.size(), kNacpSize);
    for (int lang = 0; lang < kNacpLanguages; ++lang) {
        EXPECT_EQ(nacpName(nacp, lang), spec.name);
        EXPECT_EQ(nacpPublisher(nacp, lang), "RetroManager");
    }
    EXPECT_EQ(u32(nacp, 0x302C), 0xFFFFu);                       // supported languages
    EXPECT_EQ(u64(nacp, 0x3038), spec.titleId);                  // presence group
    EXPECT_EQ(std::string(reinterpret_cast<const char*>(nacp.data() + 0x3060)), "1.2.3");
    EXPECT_EQ(u64(nacp, 0x3070), spec.titleId + 0x1000);         // add-on base
    EXPECT_EQ(u64(nacp, 0x3078), spec.titleId);                  // save data owner
    EXPECT_EQ(u64(nacp, 0x3080), 0u);                            // no save data
    EXPECT_EQ(nacp[0x3025], 0);                                  // no user account prompt
}

TEST(NacpTest, LongNamesAreCutOnACharacterBoundary) {
    NacpSpec spec;
    spec.name = std::string(0x1FE, 'a') + "é";  // 'é' would straddle the limit
    Bytes nacp = buildNacp(spec);
    EXPECT_EQ(nacpName(nacp), std::string(0x1FE, 'a'));
    EXPECT_EQ(nacp[0x1FF], 0);
}

TEST(NacpTest, LanguageNames) {
    EXPECT_STREQ(nacpLanguageNames()[0], "AmericanEnglish");
    EXPECT_STREQ(nacpLanguageNames()[3], "French");
    EXPECT_STREQ(nacpLanguageNames()[15], "BrazilianPortuguese");
}

TEST(CnmtTest, ApplicationLayoutAndRoundTrip) {
    ContentRecord program;
    program.hash.fill(0x11);
    program.size = 0x123456789Aull;
    program.type = CnmtContentType::Program;
    ContentRecord control;
    control.hash.fill(0x22);
    control.hash[0] = 0xAB;
    control.size = 0x8000;
    control.type = CnmtContentType::Control;

    std::uint64_t tid = 0x0500000000abc000ull;
    Bytes cnmt = buildApplicationCnmt(tid, 0, {program, control});
    EXPECT_EQ(cnmt.size(), 0x20u + 0x10 + 2 * 0x38 + 0x20);
    EXPECT_EQ(u64(cnmt, 0), tid);
    EXPECT_EQ(cnmt[0x0C], kCnmtApplication);
    EXPECT_EQ(u32(cnmt, 0x0E) & 0xFFFF, 0x10u);          // extended header size
    EXPECT_EQ(u32(cnmt, 0x10) & 0xFFFF, 2u);             // content count
    EXPECT_EQ(u64(cnmt, 0x20), tid + 0x800);             // patch id
    EXPECT_EQ(cnmt[0x30 + 0x38 + 0x20], 0xAB);           // content id = start of the hash
    EXPECT_EQ(cnmt[0x30 + 0x36], 1);                     // Program
    EXPECT_EQ(cnmt[0x30 + 0x38 + 0x36], 3);              // Control

    auto info = readCnmt(cnmt);
    ASSERT_TRUE(info.ok()) << info.error().describe();
    EXPECT_EQ(info.value().titleId, tid);
    EXPECT_EQ(info.value().patchId, tid + 0x800);
    ASSERT_EQ(info.value().contents.size(), 2u);
    EXPECT_EQ(info.value().contents[0].size, 0x123456789Aull);
    EXPECT_EQ(info.value().contents[1].contentId(), "ab222222222222222222222222222222");
    EXPECT_EQ(cnmtFileName(tid), "Application_0500000000abc000.cnmt");
}

TEST(NpdmTest, PatchesProgramIdAndAcidRange) {
    auto patched = patchNpdmTitleId(fakeNpdm(), 0x0511223344556000ull);
    ASSERT_TRUE(patched.ok()) << patched.error().describe();
    const Bytes& n = patched.value();
    EXPECT_EQ(u64(n, 0x80 + 0x240 + 0x10), 0x0511223344556000ull);
    EXPECT_EQ(u64(n, 0x80 + 0x210), 0x0511223344556000ull);
    EXPECT_EQ(u64(n, 0x80 + 0x218), 0x0511223344556000ull);
    EXPECT_EQ(npdmTitleId(n).value(), 0x0511223344556000ull);
}

TEST(NpdmTest, RejectsWhatIsNotAnNpdm) {
    EXPECT_EQ(patchNpdmTitleId(Bytes(0x100, 0), 1).error().code, ErrorCode::ParseError);
    Bytes truncated = fakeNpdm();
    truncated.resize(0x200);
    EXPECT_FALSE(patchNpdmTitleId(truncated, 1).ok());
    Bytes badAcid = fakeNpdm();
    badAcid[0x80 + 0x200] = 'X';
    EXPECT_FALSE(patchNpdmTitleId(badAcid, 1).ok());
}

TEST(TitleIdTest, ApplicationFormat0100xxxxxxxx0000) {
    std::uint64_t a = forwarderTitleId("/roms/gba/Pokemon.gba");
    EXPECT_EQ(a, forwarderTitleId("/roms/gba/Pokemon.gba"));  // stable: regenerating replaces the same title
    EXPECT_NE(a, forwarderTitleId("/roms/gba/Zelda.gba"));
    std::string hex = titleIdHex(a);
    EXPECT_EQ(hex.substr(0, 4), "0100") << hex;
    EXPECT_EQ(hex.substr(12), "0000") << hex;
    EXPECT_EQ(a & 0xFFFF, 0u);
    // Inside the application range 0x0100000000010000..0x01FFFFFFFFFFFFFF.
    EXPECT_GE(a, 0x0100000000010000ull);
    // Room for its update (+0x800) and add-ons (+0x1000...) without touching another forwarder.
    for (int i = 0; i < 2000; ++i) {
        std::uint64_t t = forwarderTitleId("/roms/gba/Game " + std::to_string(i) + ".gba");
        ASSERT_EQ(t >> 48, 0x0100u);
        ASSERT_EQ(t & 0xFFFF, 0u);
        ASSERT_NE((t >> 16) & 0xFFFFFFFF, 0u);
    }
    EXPECT_EQ(titleIdHex(0x0500000000abc000ull), "0500000000abc000");
}
