#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>

#include "retromanager/forwarder/nsp/Nca.hpp"

using namespace rm;
using namespace rm::nsp;

namespace {

// Fake test keys: patterns, never real console keys.
NcaKeys fakeKeys() {
    NcaKeys k;
    for (int i = 0; i < 16; ++i) {
        k.headerDataKey[i] = static_cast<std::uint8_t>(i);
        k.headerTweakKey[i] = static_cast<std::uint8_t>(0x10 + i);
        k.keyAreaKey[i] = static_cast<std::uint8_t>(0xA0 + i);
    }
    return k;
}

crypto::AesKey keyOf(std::uint8_t seed) {
    crypto::AesKey k;
    for (int i = 0; i < 16; ++i) k[i] = static_cast<std::uint8_t>(seed * 17 + i);
    return k;
}

Bytes text(const std::string& s) { return Bytes(s.begin(), s.end()); }

Bytes pattern(std::size_t size, int seed) {
    Bytes b(size);
    for (std::size_t i = 0; i < size; ++i) b[i] = static_cast<std::uint8_t>((i * 31 + seed) ^ (i >> 8));
    return b;
}

void put32(Bytes& b, std::size_t at, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) b[at + i] = static_cast<std::uint8_t>(v >> (8 * i));
}

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

constexpr std::uint64_t kTid = 0x0500000000123000ull;

ApplicationSpec sampleSpec() {
    ApplicationSpec spec;
    spec.titleId = kTid;
    spec.exefs = {{"main", pattern(0x12345, 1)}, {"main.npdm", fakeNpdm()}};
    spec.romfs = {{"nextNroPath", text("sdmc:/retroarch/cores/mgba_libretro_libnx.nro")},
                  {"nextArgv", text("\"sdmc:/retroarch/cores/mgba_libretro_libnx.nro\" \"sdmc:/roms/gba/Game.gba\"")}};
    NacpSpec nacp;
    nacp.titleId = kTid;
    nacp.name = "Game";
    nacp.publisher = "RetroManager";
    spec.nacp = buildNacp(nacp);
    spec.icon = pattern(5000, 9);
    return spec;
}

ContentKeys sampleContentKeys() { return ContentKeys{keyOf(1), keyOf(2), keyOf(3)}; }

}  // namespace

TEST(NcaTest, RoundTripBothSectionKinds) {
    NcaSpec spec;
    spec.type = ContentType::Program;
    spec.titleId = kTid;
    spec.contentKey = keyOf(7);
    spec.sections = {{NcaSection::Kind::Pfs0, buildPfs0({{"main", pattern(0x3000, 2)}})},
                     {NcaSection::Kind::RomFs, buildRomFs({{"a", pattern(0x9000, 3)}})}};
    Bytes nca = buildNca(spec, fakeKeys());
    EXPECT_EQ(nca.size() % 0x200, 0u);
    // Encrypted: neither the magic nor the payload shows in clear.
    EXPECT_NE(std::memcmp(nca.data() + 0x200, "NCA3", 4), 0);

    auto back = readNca(nca, fakeKeys());
    ASSERT_TRUE(back.ok()) << back.error().describe();
    EXPECT_EQ(back.value().type, ContentType::Program);
    EXPECT_EQ(back.value().titleId, kTid);
    EXPECT_EQ(back.value().contentSize, nca.size());
    ASSERT_EQ(back.value().sections.size(), 2u);
    EXPECT_EQ(back.value().sections[0].kind, NcaSection::Kind::Pfs0);
    EXPECT_EQ(back.value().sections[0].payload, spec.sections[0].payload);
    EXPECT_EQ(back.value().sections[1].kind, NcaSection::Kind::RomFs);
    EXPECT_EQ(back.value().sections[1].payload, spec.sections[1].payload);
}

TEST(NcaTest, HeaderLayoutAfterDecryption) {
    NcaSpec spec;
    spec.type = ContentType::Control;
    spec.titleId = kTid;
    spec.contentKey = keyOf(4);
    spec.sections = {{NcaSection::Kind::RomFs, buildRomFs({{"control.nacp", Bytes(kNacpSize, 0)}})}};
    Bytes nca = buildNca(spec, fakeKeys());
    Bytes header(nca.begin(), nca.begin() + 0xC00);
    crypto::xtsDecrypt(fakeKeys().headerDataKey, fakeKeys().headerTweakKey, header.data(), header.size(), 0x200, 0);
    EXPECT_EQ(std::memcmp(header.data() + 0x200, "NCA3", 4), 0);
    EXPECT_EQ(header[0x205], 2);                   // Control
    EXPECT_EQ(header[0x240], 6);                   // section 0 starts right after the header (0xC00 / 0x200)
    EXPECT_EQ(header[0x400 + 2], 0);               // RomFS
    EXPECT_EQ(header[0x400 + 3], 3);               // IVFC
    EXPECT_EQ(header[0x400 + 4], 3);               // AES-CTR
    // Key area slot 2 holds the content key, ECB-encrypted with the key area key.
    Bytes area(header.begin() + 0x300, header.begin() + 0x340);
    crypto::ecbDecrypt(fakeKeys().keyAreaKey, area.data(), area.size());
    EXPECT_TRUE(std::equal(spec.contentKey.begin(), spec.contentKey.end(), area.begin() + 0x20));
}

TEST(NcaTest, WrongKeysOrTamperingAreDetected) {
    NcaSpec spec;
    spec.titleId = kTid;
    spec.contentKey = keyOf(5);
    spec.sections = {{NcaSection::Kind::Pfs0, buildPfs0({{"main", pattern(0x800, 4)}})}};
    Bytes nca = buildNca(spec, fakeKeys());

    NcaKeys wrongHeader = fakeKeys();
    wrongHeader.headerDataKey[0] ^= 1;
    EXPECT_EQ(readNca(nca, wrongHeader).error().code, ErrorCode::ParseError);

    NcaKeys wrongKeyArea = fakeKeys();
    wrongKeyArea.keyAreaKey[0] ^= 1;
    EXPECT_FALSE(readNca(nca, wrongKeyArea).ok());

    Bytes tampered = nca;
    tampered[0xC00 + 0x250] ^= 0x01;  // inside the PFS0
    EXPECT_EQ(readNca(tampered, fakeKeys()).error().code, ErrorCode::IntegrityError);

    Bytes truncated(nca.begin(), nca.end() - 0x200);
    EXPECT_FALSE(readNca(truncated, fakeKeys()).ok());
}

TEST(ApplicationNspTest, RoundTripChecksEverything) {
    auto nsp = buildApplicationNsp(sampleSpec(), fakeKeys(), sampleContentKeys());
    ASSERT_TRUE(nsp.ok()) << nsp.error().describe();
    auto app = readApplicationNsp(nsp.value(), fakeKeys());
    ASSERT_TRUE(app.ok()) << app.error().describe();
    EXPECT_EQ(app.value().titleId, kTid);
    ASSERT_EQ(app.value().fileNames.size(), 3u);
    EXPECT_NE(app.value().fileNames[2].find(".cnmt.nca"), std::string::npos);
    EXPECT_EQ(app.value().cnmt.type, kCnmtApplication);
    ASSERT_EQ(app.value().cnmt.contents.size(), 2u);

    // ExeFS: main untouched, main.npdm now carries the title id.
    ASSERT_EQ(app.value().exefs.size(), 2u);
    EXPECT_EQ(app.value().exefs[0].data, sampleSpec().exefs[0].data);
    EXPECT_EQ(npdmTitleId(app.value().exefs[1].data).value(), kTid);

    // RomFS: the launch arguments.
    ASSERT_EQ(app.value().romfs.size(), 2u);
    EXPECT_EQ(app.value().romfs[1].name, "nextNroPath");

    // Control: the NACP and one icon per language.
    ASSERT_EQ(app.value().control.size(), 1u + kNacpLanguages);
    bool sawNacp = false, sawFrench = false;
    for (const auto& f : app.value().control) {
        if (f.name == "control.nacp") sawNacp = nacpName(f.data) == "Game";
        if (f.name == "icon_French.dat") sawFrench = f.data == sampleSpec().icon;
    }
    EXPECT_TRUE(sawNacp);
    EXPECT_TRUE(sawFrench);
}

TEST(ApplicationNspTest, SameInputsSameBytes) {
    auto a = buildApplicationNsp(sampleSpec(), fakeKeys(), sampleContentKeys());
    auto b = buildApplicationNsp(sampleSpec(), fakeKeys(), sampleContentKeys());
    ASSERT_TRUE(a.ok() && b.ok());
    EXPECT_EQ(a.value(), b.value());
}

TEST(ApplicationNspTest, TamperedNcaIsRejected) {
    Bytes nsp = buildApplicationNsp(sampleSpec(), fakeKeys(), sampleContentKeys()).value();
    auto entries = readPfs0(nsp).value();
    nsp[entries[0].offset + 0x2000] ^= 0x80;
    auto app = readApplicationNsp(nsp, fakeKeys());
    ASSERT_FALSE(app.ok());
    EXPECT_EQ(app.error().code, ErrorCode::IntegrityError);
}

TEST(ApplicationNspTest, RejectsIncompleteSpecs) {
    ApplicationSpec noNpdm = sampleSpec();
    noNpdm.exefs.pop_back();
    EXPECT_EQ(buildApplicationNsp(noNpdm, fakeKeys(), sampleContentKeys()).error().code, ErrorCode::NotFound);
    ApplicationSpec badNpdm = sampleSpec();
    badNpdm.exefs[1].data = text("not an npdm");
    EXPECT_EQ(buildApplicationNsp(badNpdm, fakeKeys(), sampleContentKeys()).error().code, ErrorCode::ParseError);
    ApplicationSpec noRomfs = sampleSpec();
    noRomfs.romfs.clear();
    EXPECT_FALSE(buildApplicationNsp(noRomfs, fakeKeys(), sampleContentKeys()).ok());
}

TEST(ApplicationNspTest, OptionalLogoSection) {
    ApplicationSpec spec = sampleSpec();
    spec.logo = {{"NintendoLogo.png", pattern(300, 5)}, {"StartupMovie.gif", pattern(400, 6)}};
    Bytes nsp = buildApplicationNsp(spec, fakeKeys(), sampleContentKeys()).value();
    ASSERT_TRUE(readApplicationNsp(nsp, fakeKeys()).ok());
    auto files = extractPfs0(nsp).value();
    auto program = readNca(files[0].data, fakeKeys());
    ASSERT_TRUE(program.ok());
    ASSERT_EQ(program.value().sections.size(), 3u);
    EXPECT_EQ(extractPfs0(program.value().sections[2].payload).value()[0].name, "NintendoLogo.png");
}

// Black-box cross-check with an independent implementation: set RM_HACTOOL
// to a hactool binary to run it (skipped otherwise, e.g. in CI). Every hash
// must be GOOD (signatures cannot be: they need Nintendo's private keys) and
// the sections hactool decrypts must be byte-identical to ours.
namespace {

std::string readText(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), {});
}

std::string runHactool(const std::string& hactool, const std::string& args, const std::string& log) {
    std::string cmd = hactool + " " + args + " > " + log + " 2>&1";
    int rc = std::system(cmd.c_str());
    std::string out = readText(log);
    EXPECT_EQ(rc, 0) << out;
    return out;
}

void expectOnlySignaturesFail(const std::string& output, const std::string& what) {
    std::size_t pos = 0;
    while ((pos = output.find("(FAIL)", pos)) != std::string::npos) {
        std::size_t lineStart = output.rfind('\n', pos);
        std::string line = output.substr(lineStart == std::string::npos ? 0 : lineStart + 1, pos - lineStart);
        EXPECT_NE(line.find("Signature"), std::string::npos) << what << ": " << line;
        ++pos;
    }
    EXPECT_NE(output.find("(GOOD)"), std::string::npos) << what;
}

}  // namespace

TEST(ApplicationNspTest, HactoolAcceptsTheNcas) {
    const char* hactool = std::getenv("RM_HACTOOL");
    if (hactool == nullptr || *hactool == '\0') GTEST_SKIP() << "RM_HACTOOL not set";
    std::string dir = ::testing::TempDir() + "rm_hactool/";
    ASSERT_EQ(std::system(("rm -rf " + dir + " && mkdir -p " + dir).c_str()), 0);
    std::string keysPath = dir + "fake.keys";
    std::ofstream(keysPath) << "header_key = 000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f\n"
                            << "key_area_key_application_00 = a0a1a2a3a4a5a6a7a8a9aaabacadaeaf\n";
    ApplicationSpec spec = sampleSpec();
    Bytes nsp = buildApplicationNsp(spec, fakeKeys(), sampleContentKeys()).value();
    int checked = 0;
    for (const auto& f : extractPfs0(nsp).value()) {
        std::string path = dir + f.name;
        std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(f.data.data()),
                                                    static_cast<std::streamsize>(f.data.size()));
        std::string out = runHactool(hactool, "-k " + keysPath + " -y " + path, path + ".log");
        expectOnlySignaturesFail(out, f.name);
        std::string x = path + ".x/";
        ASSERT_EQ(std::system(("mkdir -p " + x).c_str()), 0);  // hactool creates only the last level
        runHactool(hactool, "-k " + keysPath + " --section0dir=" + x + "s0 --exefsdir=" + x + "exefs --romfsdir=" + x + "romfs " + path,
                   path + ".xlog");
        if (out.find("Content Type:                       Program") != std::string::npos) {
            EXPECT_TRUE(readText(x + "exefs/main") == std::string(spec.exefs[0].data.begin(), spec.exefs[0].data.end()));
            EXPECT_EQ(readText(x + "romfs/nextNroPath"), std::string(spec.romfs[0].data.begin(), spec.romfs[0].data.end()));
            EXPECT_EQ(readText(x + "romfs/nextArgv"), std::string(spec.romfs[1].data.begin(), spec.romfs[1].data.end()));
            ++checked;
        } else if (out.find("Content Type:                       Control") != std::string::npos) {
            EXPECT_TRUE(readText(x + "romfs/control.nacp") == std::string(spec.nacp.begin(), spec.nacp.end()));
            EXPECT_TRUE(readText(x + "romfs/icon_Japanese.dat") == std::string(spec.icon.begin(), spec.icon.end()));
            ++checked;
        } else if (out.find("Content Type:                       Meta") != std::string::npos) {
            EXPECT_EQ(readText(x + "s0/" + cnmtFileName(kTid)).size(), 0x20u + 0x10 + 2 * 0x38 + 0x20);
            ++checked;
        }
    }
    EXPECT_EQ(checked, 3);

    // The NSP itself, as a PFS0.
    std::string nspPath = dir + "Game.nsp";
    std::ofstream(nspPath, std::ios::binary).write(reinterpret_cast<const char*>(nsp.data()), static_cast<std::streamsize>(nsp.size()));
    std::string out = runHactool(hactool, "-t pfs0 " + nspPath, nspPath + ".log");
    EXPECT_NE(out.find("Number of files:                    3"), std::string::npos) << out;
    EXPECT_NE(out.find(".cnmt.nca"), std::string::npos) << out;
}
