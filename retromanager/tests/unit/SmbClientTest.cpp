#include <gtest/gtest.h>

#include "retromanager/network/SmbClient.hpp"

using namespace rm;

namespace {

SmbConfig zima() {
    SmbConfig c;
    c.host = "192.168.1.102";
    c.share = "HDD-Storage1";
    c.indexPath = "/roms ds/shop.json";
    c.username = "leo";
    c.password = "s3cret";
    return c;
}

}  // namespace

TEST(SmbConfig, FromTheUrlOfAZimaOsShare) {
    auto c = smbConfigFromUrl("smb://192.168.1.102/HDD-Storage1/roms ds/shop.json", "leo", "pw");
    ASSERT_TRUE(c.ok()) << c.error().describe();
    EXPECT_EQ(c.value().host, "192.168.1.102");
    EXPECT_EQ(c.value().port, 445);
    EXPECT_EQ(c.value().share, "HDD-Storage1");
    EXPECT_EQ(c.value().indexPath, "/roms ds/shop.json");
    EXPECT_EQ(c.value().username, "leo");
    EXPECT_EQ(c.value().password, "pw");

    auto encoded = smbConfigFromUrl("smb://nas:4450/Share/roms%20ds/", "", "");
    ASSERT_TRUE(encoded.ok());
    EXPECT_EQ(encoded.value().port, 4450);
    EXPECT_EQ(encoded.value().indexPath, "/roms ds/index.json");
    EXPECT_EQ(encoded.value().username, "");  // guest
}

TEST(SmbConfig, CredentialsAndDomainFromTheUrl) {
    auto c = smbConfigFromUrl("smb://WORK;leo:p%40ss@nas/Share/index.json", "", "");
    ASSERT_TRUE(c.ok()) << c.error().describe();
    EXPECT_EQ(c.value().domain, "WORK");
    EXPECT_EQ(c.value().username, "leo");
    EXPECT_EQ(c.value().password, "p@ss");
    auto fields = smbConfigFromUrl("smb://url:user@nas/Share/", "field", "wins");
    EXPECT_EQ(fields.value().username, "field");
    EXPECT_EQ(fields.value().password, "wins");
}

TEST(SmbConfig, RejectsWhatIsNotAShare) {
    EXPECT_EQ(smbConfigFromUrl("", "", "").error().code, ErrorCode::NotConfigured);
    EXPECT_EQ(smbConfigFromUrl("smb://nas/", "", "").error().code, ErrorCode::InvalidArgument);  // no share
    EXPECT_EQ(smbConfigFromUrl("smb://nas", "", "").error().code, ErrorCode::InvalidArgument);
    EXPECT_EQ(smbConfigFromUrl("ftp://nas/Share/", "", "").error().code, ErrorCode::Unsupported);
    EXPECT_FALSE(smbConfigFromUrl("smb://nas:99999/Share/", "", "").ok());
}

TEST(SmbClient, UrlsAndDescriptionNeverContainThePassword) {
    SmbClient client(zima());
    EXPECT_EQ(client.indexUrl(), "smb://192.168.1.102:445/HDD-Storage1/roms%20ds/shop.json");
    EXPECT_EQ(client.describe().find("s3cret"), std::string::npos);
    EXPECT_NE(client.describe().find("leo@"), std::string::npos);
    SmbConfig guest = zima();
    guest.username.clear();
    EXPECT_NE(SmbClient(guest).describe().find("guest@"), std::string::npos);
}

TEST(SmbClient, OnlyItsOwnHostPortAndShare) {
    SmbClient client(zima());
    EXPECT_EQ(client.pathOnServer("smb://192.168.1.102/HDD-Storage1/roms%20ds/Mario Kart.nds").value(),
              "/roms ds/Mario Kart.nds");
    EXPECT_EQ(client.pathOnServer("smb://192.168.1.102:445/hdd-storage1/a.nds").value(), "/a.nds");  // shares ignore case
    EXPECT_EQ(client.pathOnServer("smb://192.168.1.103/HDD-Storage1/a.nds").error().code, ErrorCode::PermissionDenied);
    EXPECT_EQ(client.pathOnServer("smb://192.168.1.102:4450/HDD-Storage1/a.nds").error().code, ErrorCode::PermissionDenied);
    EXPECT_EQ(client.pathOnServer("smb://192.168.1.102/Other/a.nds").error().code, ErrorCode::PermissionDenied);
    EXPECT_EQ(client.pathOnServer("ftp://192.168.1.102/HDD-Storage1/a.nds").error().code, ErrorCode::PermissionDenied);
    // ".." is resolved inside the share and can never climb out of it.
    EXPECT_EQ(client.pathOnServer("smb://192.168.1.102/HDD-Storage1/roms/../bios/x.bin").value(), "/bios/x.bin");
    EXPECT_FALSE(client.pathOnServer("smb://192.168.1.102/HDD-Storage1/roms/../../etc/passwd").ok());
}

TEST(SmbClient, ValidatesConfiguration) {
    EXPECT_TRUE(SmbClient::validate(zima()).ok());
    SmbConfig c = zima();
    c.host.clear();
    EXPECT_FALSE(SmbClient::validate(c).ok());
    c = zima();
    c.share.clear();
    EXPECT_FALSE(SmbClient::validate(c).ok());
    c = zima();
    c.share = "a/b";
    EXPECT_FALSE(SmbClient::validate(c).ok());
    c = zima();
    c.indexPath = "shop.json";
    EXPECT_FALSE(SmbClient::validate(c).ok());
}
