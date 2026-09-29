// FTP listing parser (MLSD, RFC 3659): pure functions of FtpClient.

#include <gtest/gtest.h>

#include "retromanager/network/FtpClient.hpp"

using namespace rm;

TEST(Mlsd, ParsesFactsAndNames) {
    const std::string listing =
        "type=cdir;modify=20260928120000;perm=flcdmpe; .\r\n"
        "type=pdir;modify=20260928120000;perm=flcdmpe; ..\r\n"
        "type=file;size=8192;modify=20260928123456;perm=adfrw; Pokemon Platine (France).sav\r\n"
        "Type=File;Size=32;Modify=19700101000001.123; tiny.srm\r\n"
        "type=dir;modify=20260101000000; mGBA\r\n"
        "type=OS.unix=symlink;size=4; link\r\n";
    std::vector<RemoteEntry> entries = FtpClient::parseMlsd(listing);

    ASSERT_EQ(entries.size(), 3u);  // cdir, pdir and the symlink are dropped
    EXPECT_EQ(entries[0].name, "Pokemon Platine (France).sav");
    EXPECT_FALSE(entries[0].isDirectory);
    EXPECT_EQ(entries[0].size, 8192u);
    EXPECT_EQ(entries[0].modifiedAt, 1790598896);  // 2026-09-28 12:34:56 UTC
    EXPECT_EQ(entries[1].name, "tiny.srm");
    EXPECT_EQ(entries[1].modifiedAt, 1);  // facts are case-insensitive, fractions ignored
    EXPECT_EQ(entries[2].name, "mGBA");
    EXPECT_TRUE(entries[2].isDirectory);
}

TEST(Mlsd, KeepsSpacesInNamesAndToleratesMissingFacts) {
    auto entries = FtpClient::parseMlsd("type=file; name with  two spaces.srm\nsize=3; nameless-type.srm\n\n");
    ASSERT_EQ(entries.size(), 2u);
    EXPECT_EQ(entries[0].name, "name with  two spaces.srm");
    EXPECT_FALSE(entries[0].modifiedAt.has_value());
    EXPECT_EQ(entries[1].size, 3u);
}

TEST(Mlsd, ConvertsTimestamps) {
    EXPECT_EQ(FtpClient::parseMlsdTime("19700101000000"), 0);
    EXPECT_EQ(FtpClient::parseMlsdTime("20000229235959"), 951868799);  // leap day
    EXPECT_EQ(FtpClient::parseMlsdTime("20260928123456.789"), 1790598896);
    EXPECT_FALSE(FtpClient::parseMlsdTime("2026").has_value());
    EXPECT_FALSE(FtpClient::parseMlsdTime("20261328123456").has_value());  // month 13
    EXPECT_FALSE(FtpClient::parseMlsdTime("2026092812345x").has_value());
}
