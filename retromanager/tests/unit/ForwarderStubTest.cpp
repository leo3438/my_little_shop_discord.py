#include <gtest/gtest.h>

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>

#include "../../tools/forwarder-stub/source/romfs_lookup.h"
#include "retromanager/forwarder/nsp/Containers.hpp"
#include "retromanager/forwarder/nsp/Metadata.hpp"

using namespace rm;
using namespace rm::nsp;

// The forwarder stub (tools/forwarder-stub) reads romfs:/nextNroPath and
// romfs:/nextArgv with romfs_lookup.c: it must find them in the RomFS that
// the NSP writer produces.

namespace {

Bytes text(const std::string& s) { return Bytes(s.begin(), s.end()); }

struct Blob {
    const Bytes* bytes;
    std::size_t readLimit;  // simulated storage size
};

bool readBlob(void* ctx, std::uint64_t offset, void* buf, std::size_t size) {
    const Blob* blob = static_cast<const Blob*>(ctx);
    if (offset > blob->readLimit || blob->readLimit - offset < size) return false;
    std::memcpy(buf, blob->bytes->data() + offset, size);
    return true;
}

long lookup(const Bytes& romfs, const char* name, std::string& out, std::size_t cap = 2048,
            std::size_t limit = static_cast<std::size_t>(-1)) {
    Blob blob{&romfs, limit == static_cast<std::size_t>(-1) ? romfs.size() : limit};
    std::string buffer(cap, 'X');
    long n = rl_read_root_file(readBlob, &blob, name, buffer.data(), cap);
    out = n >= 0 ? std::string(buffer.c_str()) : std::string();
    return n;
}

const std::string kNro = "sdmc:/retroarch/cores/mgba_libretro_libnx.nro";
const std::string kArgv = "\"" + kNro + "\" \"sdmc:/roms/gba/Advance Wars (Europe).gba\"";

Bytes forwarderRomFs() {
    return buildRomFs({{"nextNroPath", text(kNro)}, {"nextArgv", text(kArgv)}, {"nextArgvExtra", text("decoy")}, {"a", text("")}});
}

std::uint32_t le32(const Bytes& b, std::size_t at) {
    return b[at] | (b[at + 1] << 8) | (b[at + 2] << 16) | (static_cast<std::uint32_t>(b[at + 3]) << 24);
}

// Program type kernel capabilities ((type << 14) | 0x1FFF) of an ACI0/ACID
// kernel capability block.
std::vector<std::uint32_t> programTypes(const Bytes& npdm, std::size_t block, std::size_t offsetField) {
    std::vector<std::uint32_t> types;
    std::size_t kc = block + le32(npdm, block + offsetField), size = le32(npdm, block + offsetField + 4);
    for (std::size_t at = kc; at + 4 <= kc + size && at + 4 <= npdm.size(); at += 4) {
        std::uint32_t cap = le32(npdm, at);
        if ((cap & 0x3FFF) == 0x1FFF) types.push_back(cap >> 14);
    }
    return types;
}

Bytes readFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return Bytes(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

}  // namespace

TEST(ForwarderStubRomfs, FindsTheLaunchFilesOfTheNspWriter) {
    Bytes romfs = forwarderRomFs();
    std::string out;
    EXPECT_EQ(lookup(romfs, "nextNroPath", out), static_cast<long>(kNro.size()));
    EXPECT_EQ(out, kNro);
    EXPECT_EQ(lookup(romfs, "nextArgv", out), static_cast<long>(kArgv.size()));
    EXPECT_EQ(out, kArgv);  // not "nextArgvExtra", which shares the prefix
    EXPECT_EQ(lookup(romfs, "a", out), 0);
}

TEST(ForwarderStubRomfs, MissingOrOversizedFilesAreRejected) {
    Bytes romfs = forwarderRomFs();
    std::string out;
    EXPECT_EQ(lookup(romfs, "hbmenu.nro", out), -1);
    EXPECT_EQ(lookup(romfs, "nextNroPath", out, kNro.size()), -1);  // no room for the NUL
    EXPECT_EQ(lookup(romfs, "nextNroPath", out, kNro.size() + 1), static_cast<long>(kNro.size()));
    EXPECT_EQ(lookup(romfs, "nextNroPath", out, 0), -1);
}

TEST(ForwarderStubRomfs, CorruptOrTruncatedStorageFailsCleanly) {
    Bytes romfs = forwarderRomFs();
    std::string out;
    EXPECT_EQ(lookup(romfs, "nextNroPath", out, 2048, 0x40), -1);  // header cut
    for (std::size_t limit = 0x50; limit < romfs.size(); limit += 7) {
        long n = lookup(romfs, "nextArgv", out, 2048, limit);
        EXPECT_TRUE(n == -1 || n == static_cast<long>(kArgv.size())) << limit;
    }
    Bytes notRomfs = romfs;
    notRomfs[0] = 0x51;
    EXPECT_EQ(lookup(notRomfs, "nextNroPath", out), -1);
    Bytes nulInside = buildRomFs({{"nextNroPath", Bytes{'s', 0, 'x'}}});
    EXPECT_EQ(lookup(nulInside, "nextNroPath", out), -1);
}

// The stub built by CI (tools/forwarder-stub/build.sh): set RM_TEST_STUB_DIR
// to the folder holding main and main.npdm.
TEST(ForwarderStubBuild, IsAnApplicationExeFs) {
    const char* dir = std::getenv("RM_TEST_STUB_DIR");
    if (dir == nullptr || *dir == '\0') GTEST_SKIP() << "RM_TEST_STUB_DIR not set";
    Bytes main = readFile(std::string(dir) + "/main");
    Bytes npdm = readFile(std::string(dir) + "/main.npdm");
    ASSERT_GE(main.size(), 0x100u);
    EXPECT_EQ(std::memcmp(main.data(), "NSO0", 4), 0);

    ASSERT_TRUE(npdmTitleId(npdm)) << npdmTitleId(npdm).error().describe();
    const std::size_t aci0 = le32(npdm, 0x70), acid = le32(npdm, 0x78);
    EXPECT_EQ(programTypes(npdm, aci0, 0x30), std::vector<std::uint32_t>{1}) << "ACI0 must declare an application";
    EXPECT_EQ(programTypes(npdm, acid, 0x230), std::vector<std::uint32_t>{1}) << "ACID must declare an application";

    auto patched = patchNpdmTitleId(npdm, 0x0100123456780000ULL);
    ASSERT_TRUE(patched);
    EXPECT_EQ(npdmTitleId(patched.value()).value(), 0x0100123456780000ULL);
}
