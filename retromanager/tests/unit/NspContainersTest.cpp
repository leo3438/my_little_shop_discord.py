#include <gtest/gtest.h>

#include <cstring>

#include "retromanager/forwarder/nsp/Containers.hpp"

using namespace rm;
using namespace rm::nsp;

namespace {

Bytes text(const std::string& s) { return Bytes(s.begin(), s.end()); }

std::uint32_t u32(const Bytes& b, std::size_t at) {
    return b[at] | (b[at + 1] << 8) | (b[at + 2] << 16) | (static_cast<std::uint32_t>(b[at + 3]) << 24);
}
std::uint64_t u64(const Bytes& b, std::size_t at) { return u32(b, at) | (static_cast<std::uint64_t>(u32(b, at + 4)) << 32); }

Bytes pattern(std::size_t size, int seed) {
    Bytes b(size);
    for (std::size_t i = 0; i < size; ++i) b[i] = static_cast<std::uint8_t>((i * 31 + seed) ^ (i >> 8));
    return b;
}

}  // namespace

TEST(Pfs0Test, LayoutMatchesTheFormat) {
    Bytes pfs = buildPfs0({{"main", text("CODE")}, {"main.npdm", text("META!")}});
    ASSERT_GE(pfs.size(), 0x10u);
    EXPECT_EQ(std::memcmp(pfs.data(), "PFS0", 4), 0);
    EXPECT_EQ(u32(pfs, 4), 2u);                       // file count
    std::uint32_t strings = u32(pfs, 8);
    std::uint64_t dataStart = 0x10 + 2 * 0x18 + strings;
    EXPECT_EQ(dataStart % 0x20, 0u);                    // header padded
    EXPECT_EQ(dataStart, pfs0HeaderSize({"main", "main.npdm"}));
    EXPECT_EQ(u64(pfs, 0x10), 0u);                      // first file, offset in the data area
    EXPECT_EQ(u64(pfs, 0x18), 4u);
    EXPECT_EQ(u64(pfs, 0x28), 4u);                      // second file follows
    EXPECT_EQ(std::string(reinterpret_cast<const char*>(pfs.data() + 0x10 + 2 * 0x18 + u32(pfs, 0x38))), "main.npdm");
    EXPECT_EQ(pfs.size(), dataStart + 9);
}

TEST(Pfs0Test, RoundTrip) {
    std::vector<NamedFile> files = {{"a.nca", pattern(1000, 1)}, {"empty", {}}, {"b.cnmt.nca", pattern(77, 2)}};
    auto back = extractPfs0(buildPfs0(files));
    ASSERT_TRUE(back.ok()) << back.error().describe();
    ASSERT_EQ(back.value().size(), 3u);
    for (std::size_t i = 0; i < files.size(); ++i) {
        EXPECT_EQ(back.value()[i].name, files[i].name);
        EXPECT_EQ(back.value()[i].data, files[i].data);
    }
}

TEST(Pfs0Test, RejectsGarbageAndTruncation) {
    EXPECT_FALSE(readPfs0(text("PFS1....")).ok());
    Bytes pfs = buildPfs0({{"main", pattern(100, 3)}});
    pfs.resize(pfs.size() - 1);
    EXPECT_FALSE(readPfs0(pfs).ok());
}

TEST(RomFsTest, HeaderAndRoundTrip) {
    std::vector<NamedFile> files = {{"nextNroPath", text("sdmc:/retroarch/cores/mgba_libretro_libnx.nro")},
                                    {"nextArgv", text("\"a\" \"b\"")},
                                    {"icon_AmericanEnglish.dat", pattern(3000, 4)}};
    Bytes romfs = buildRomFs(files);
    EXPECT_EQ(u64(romfs, 0), 0x50u);                  // header size
    EXPECT_EQ(u64(romfs, 0x48), 0x200u);              // file data offset
    auto back = readRomFs(romfs);
    ASSERT_TRUE(back.ok()) << back.error().describe();
    ASSERT_EQ(back.value().size(), 3u);
    // Sorted by name, like Nintendo's tools.
    EXPECT_EQ(back.value()[0].name, "icon_AmericanEnglish.dat");
    EXPECT_EQ(back.value()[1].name, "nextArgv");
    EXPECT_EQ(back.value()[2].name, "nextNroPath");
    EXPECT_EQ(back.value()[2].data, files[0].data);
    EXPECT_EQ(back.value()[0].data, files[2].data);
}

TEST(RomFsTest, HashTableLookupFindsEveryFile) {
    std::vector<NamedFile> files;
    for (int i = 0; i < 40; ++i) files.push_back({"file" + std::to_string(i) + ".bin", pattern(i * 13, i)});
    Bytes romfs = buildRomFs(files);
    for (const auto& f : files) {
        auto found = findRomFsFile(romfs, f.name);
        ASSERT_TRUE(found.has_value()) << f.name;
        EXPECT_EQ(*found, f.data);
    }
    EXPECT_FALSE(findRomFsFile(romfs, "missing").has_value());
}

TEST(RomFsTest, FileDataIsAligned) {
    Bytes romfs = buildRomFs({{"a", text("1")}, {"b", text("22")}});
    auto back = readRomFs(romfs);
    ASSERT_TRUE(back.ok());
    // File meta table: first entry's data offset 0, second one 0x10.
    std::uint64_t fileMeta = u64(romfs, 0x38);
    EXPECT_EQ(u64(romfs, fileMeta + 8), 0u);
    std::uint32_t second = u32(romfs, fileMeta + 4);  // sibling
    EXPECT_EQ(u64(romfs, fileMeta + second + 8), 0x10u);
}

TEST(HashTreeTest, Sha256SectionRoundTripAndTamper) {
    Bytes pfs = buildPfs0({{"main", pattern(0x2345, 5)}, {"main.npdm", pattern(0x300, 6)}});
    HashedSection section = buildSha256Section(pfs);
    EXPECT_EQ(section.hashData.size(), 0xF8u);
    EXPECT_EQ(u32(section.hashData, 0x20), 0x1000u);   // block size
    EXPECT_EQ(u32(section.hashData, 0x24), 2u);        // layers
    EXPECT_EQ(u64(section.hashData, 0x28), 0u);        // hash table offset
    EXPECT_EQ(u64(section.hashData, 0x30), 3u * 32);   // 3 blocks
    EXPECT_EQ(u64(section.hashData, 0x38), 0x200u);    // pfs0 offset
    EXPECT_EQ(u64(section.hashData, 0x40), pfs.size());
    EXPECT_EQ(section.data.size() % 0x200, 0u);

    auto back = verifySha256Section(section.data, section.hashData);
    ASSERT_TRUE(back.ok()) << back.error().describe();
    EXPECT_EQ(back.value(), pfs);

    Bytes tampered = section.data;
    tampered[0x200 + 0x2000] ^= 1;
    auto bad = verifySha256Section(tampered, section.hashData);
    ASSERT_FALSE(bad.ok());
    EXPECT_EQ(bad.error().code, ErrorCode::IntegrityError);
}

TEST(HashTreeTest, IvfcSectionRoundTripAndTamper) {
    Bytes romfs = buildRomFs({{"big", pattern(0x4000 * 3 + 123, 7)}});
    HashedSection section = buildIvfcSection(romfs);
    EXPECT_EQ(std::memcmp(section.hashData.data(), "IVFC", 4), 0);
    EXPECT_EQ(u32(section.hashData, 4), 0x20000u);
    EXPECT_EQ(u32(section.hashData, 8), 0x20u);
    EXPECT_EQ(u32(section.hashData, 12), 7u);
    // Level 6 = the RomFS itself, block 2^14.
    std::size_t level6 = 0x10 + 5 * 0x18;
    EXPECT_EQ(u64(section.hashData, level6 + 8), romfs.size());
    EXPECT_EQ(u32(section.hashData, level6 + 16), 14u);
    // Level 5 hashes the 4 blocks of level 6.
    EXPECT_EQ(u64(section.hashData, 0x10 + 4 * 0x18 + 8), 4u * 32);

    auto back = verifyIvfcSection(section.data, section.hashData);
    ASSERT_TRUE(back.ok()) << back.error().describe();
    EXPECT_EQ(back.value(), romfs);

    Bytes tampered = section.data;
    tampered[u64(section.hashData, level6) + 5] ^= 0x40;
    EXPECT_EQ(verifyIvfcSection(tampered, section.hashData).error().code, ErrorCode::IntegrityError);
    Bytes badMaster = section.hashData;
    badMaster[0xC0] ^= 1;
    EXPECT_FALSE(verifyIvfcSection(section.data, badMaster).ok());
}
