#include <gtest/gtest.h>

#include "MemoryFileSystem.hpp"
#include "retromanager/fs/FileInstall.hpp"
#include "retromanager/core/FileName.hpp"

using namespace rm;

namespace {

Status nroMagic(std::string_view header) {
    return header.substr(0x10, 4) == "NRO0" ? success() : Status(makeError(ErrorCode::IntegrityError, "not an NRO"));
}

std::string fakeNro() { return std::string(0x10, '\0') + "NRO0" + std::string(100, 'x'); }

}  // namespace

TEST(FileName, SanitizesForFat) {
    EXPECT_EQ(sanitizeFileName("Zelda: A Link <USA>?.sfc").value(), "Zelda_ A Link _USA__.sfc");
    EXPECT_EQ(sanitizeFileName("  RetroArch  ").value(), "RetroArch");
    EXPECT_EQ(sanitizeFileName("pNES...").value(), "pNES");
    for (const char* bad : {"", "..", ".", "   ", ".hidden", "a/b", "a\\b"}) {
        EXPECT_FALSE(sanitizeFileName(bad).ok()) << '"' << bad << '"';
    }
    EXPECT_FALSE(sanitizeFileName(std::string(300, 'a')).ok());
}

TEST(FileInstall, HeaderCheckRunsAsSoonAsTheHeaderIsThere) {
    test::MemoryFileSystem fs;
    auto install = beginFileInstall(fs, "/switch/App/App.nro", 0, "", "App");
    ASSERT_TRUE(install.ok()) << install.error().describe();
    install.value()->setHeaderCheck(0x14, nroMagic);

    const std::string html = "<html><body>404 Not Found</body></html>";
    EXPECT_TRUE(install.value()->write(html.data(), 8).ok());  // not enough bytes yet
    Status rejected = install.value()->write(html.data() + 8, html.size() - 8);
    EXPECT_EQ(rejected.error().code, ErrorCode::IntegrityError);  // the transfer stops here
    install.value().reset();
    EXPECT_FALSE(fs.exists("/switch/App/App.nro"));
}

TEST(FileInstall, HeaderSplitAcrossChunksIsAccepted) {
    test::MemoryFileSystem fs;
    auto install = beginFileInstall(fs, "/switch/App/App.nro", 0, "", "App");
    ASSERT_TRUE(install.ok());
    install.value()->setHeaderCheck(0x14, nroMagic);
    const std::string nro = fakeNro();
    for (char c : nro) ASSERT_TRUE(install.value()->write(&c, 1).ok());
    ASSERT_TRUE(install.value()->commit().ok());
    EXPECT_EQ(fs.readFile("/switch/App/App.nro").value(), nro);
}

TEST(FileInstall, FileShorterThanItsHeaderIsRejectedAtCommit) {
    test::MemoryFileSystem fs;
    auto install = beginFileInstall(fs, "/switch/App/App.nro", 0, "", "App");
    ASSERT_TRUE(install.ok());
    install.value()->setHeaderCheck(0x14, nroMagic);
    ASSERT_TRUE(install.value()->write("tiny", 4).ok());
    EXPECT_EQ(install.value()->commit().error().code, ErrorCode::IntegrityError);
    EXPECT_FALSE(fs.exists("/switch/App/App.nro"));
}

TEST(FileInstall, ChecksSpaceBeforeCreatingAnything) {
    test::MemoryFileSystem fs;
    fs.setCapacity(10 * 1024 * 1024);
    auto install = beginFileInstall(fs, "/switch/App/App.nro", 20 * 1024 * 1024, "", "App");
    EXPECT_EQ(install.error().code, ErrorCode::InsufficientSpace);
    EXPECT_FALSE(fs.exists("/switch/App"));

    SpaceReport report = checkSpace(fs, "/switch/App/App.nro", 20 * 1024 * 1024);
    EXPECT_FALSE(report.sufficient);
    EXPECT_EQ(report.requiredBytes, 20u * 1024 * 1024 + kSpaceMargin);
}
