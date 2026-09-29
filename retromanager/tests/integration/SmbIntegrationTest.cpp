// SmbClient against a real Samba server with its default settings (SMB2
// minimum, like ZimaOS or Synology), see tools/test_smb_server.sh.
//
// Skipped unless RM_TEST_SMB_PORT is set.

#include <gtest/gtest.h>

#include <cstdlib>

#include "retromanager/core/Crc32.hpp"
#include "retromanager/core/ITaskRunner.hpp"
#include "retromanager/network/SmbClient.hpp"
#include "retromanager/services/ShopService.hpp"

using namespace rm;

namespace {

class Smb : public ::testing::Test {
  protected:
    void SetUp() override {
        const char* port = std::getenv("RM_TEST_SMB_PORT");
        if (port == nullptr || *port == '\0') GTEST_SKIP() << "RM_TEST_SMB_PORT not set (see tools/test_smb_server.sh)";
        root = std::string("smb://127.0.0.1:") + port;
    }

    SmbClient client(const std::string& path, std::string user = "retro", std::string password = "manager") {
        auto config = smbConfigFromUrl(root + path, user, password);
        EXPECT_TRUE(config.ok()) << config.error().describe();
        config.value().timeoutSeconds = 5;
        return SmbClient(config.value());
    }

    Result<std::string> get(SmbClient& smb, const std::string& url, std::uint64_t offset = 0,
                            std::vector<TransferProgress>* ticks = nullptr, const CancellationToken* cancelWith = nullptr) {
        std::string body;
        CancellationToken none;
        Status status = smb.downloadFileFrom(
            url, offset, [&](const char* d, std::size_t n) { body.append(d, n); return success(); },
            [&](const TransferProgress& p) {
                if (ticks) ticks->push_back(p);
            },
            cancelWith ? *cancelWith : none);
        if (!status) return status.error();
        return body;
    }

    Status put(SmbClient& smb, const std::string& url, const std::string& content, const CancellationToken* cancel = nullptr) {
        std::size_t sent = 0;
        CancellationToken none;
        return smb.uploadFile(
            url,
            [&](char* buffer, std::size_t capacity) -> Result<std::size_t> {
                std::size_t n = std::min(capacity, content.size() - sent);
                std::copy_n(content.data() + sent, n, buffer);
                sent += n;
                return n;
            },
            content.size(), nullptr, cancel ? *cancel : none);
    }

    std::string root;
};

}  // namespace

// The user's case: a share with a space in the folder name, the index and
// the ROMs next to it.
TEST_F(Smb, LoadsAShopFromAShareWithSpaces) {
    SmbClient smb = client("/HDD-Storage1/shop ds/");
    ImmediateTaskRunner tasks;
    auto index = ShopService(smb, tasks).loadIndex();
    ASSERT_TRUE(index.ok()) << index.error().describe();
    EXPECT_EQ(index.value().name, "Shop with spaces");
    ASSERT_EQ(index.value().games.size(), 1u);
    EXPECT_EQ(index.value().games[0].romUrl.rfind(root + "/HDD-Storage1/shop%20ds/", 0), 0u) << index.value().games[0].romUrl;
    auto rom = get(smb, index.value().games[0].romUrl);
    ASSERT_TRUE(rom.ok()) << rom.error().describe();
    EXPECT_EQ(rom.value(), "MOCK ROM nds space\n");
}

TEST_F(Smb, TheFtpFixtureShopWorksOverSmbToo) {
    SmbClient smb = client("/HDD-Storage1/shop/index.json");
    ImmediateTaskRunner tasks;
    auto index = ShopService(smb, tasks).loadIndex();
    ASSERT_TRUE(index.ok()) << index.error().describe();
    ASSERT_FALSE(index.value().games.empty());
    auto smw = get(smb, root + "/HDD-Storage1/shop/roms/snes/Super Mario World (USA).sfc");
    ASSERT_TRUE(smw.ok()) << smw.error().describe();
    EXPECT_EQ(smw.value(), "MOCK ROM snes\n");
    auto unicode = get(smb, root + "/HDD-Storage1/shop/roms/gba/Pok%C3%A9mon%20%C3%89meraude%20(France).gba");
    ASSERT_TRUE(unicode.ok()) << unicode.error().describe();
}

TEST_F(Smb, StreamsABigFileInChunksWithProgress) {
    SmbClient smb = client("/HDD-Storage1/index.json");
    std::vector<TransferProgress> ticks;
    auto big = get(smb, root + "/HDD-Storage1/big.bin", 0, &ticks);
    ASSERT_TRUE(big.ok()) << big.error().describe();
    EXPECT_EQ(big.value().size(), 3u * 1024 * 1024);
    ASSERT_GE(ticks.size(), 3u);  // at most 1 MiB per read
    EXPECT_EQ(ticks.back().total, 3u * 1024 * 1024);
    EXPECT_EQ(ticks.back().received, 3u * 1024 * 1024);
}

TEST_F(Smb, ResumesFromAnOffset) {
    SmbClient smb = client("/HDD-Storage1/index.json");
    auto whole = get(smb, root + "/HDD-Storage1/big.bin");
    ASSERT_TRUE(whole.ok());
    std::vector<TransferProgress> ticks;
    auto tail = get(smb, root + "/HDD-Storage1/big.bin", 2 * 1024 * 1024 + 5, &ticks);
    ASSERT_TRUE(tail.ok()) << tail.error().describe();
    EXPECT_EQ(tail.value(), whole.value().substr(2 * 1024 * 1024 + 5));
    EXPECT_EQ(ticks.back().received, 3u * 1024 * 1024);  // progress counts the offset
    auto past = get(smb, root + "/HDD-Storage1/big.bin", 4 * 1024 * 1024);
    ASSERT_FALSE(past.ok());
    EXPECT_EQ(past.error().code, ErrorCode::Unsupported);
}

TEST_F(Smb, CancelStopsTheTransfer) {
    SmbClient smb = client("/HDD-Storage1/index.json");
    CancellationToken cancel;
    cancel.cancel();
    auto result = get(smb, root + "/HDD-Storage1/big.bin", 0, nullptr, &cancel);
    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.error().code, ErrorCode::Cancelled);
}

TEST_F(Smb, GuestReadsAPublicShare) {
    SmbClient smb = client("/Public/hello.txt", "", "");
    auto file = get(smb, root + "/Public/hello.txt");
    ASSERT_TRUE(file.ok()) << file.error().describe();
    EXPECT_EQ(file.value(), "public file\n");
}

TEST_F(Smb, ErrorsAreSpecificAndReadable) {
    SmbClient wrong = client("/HDD-Storage1/shop/index.json", "retro", "wrong");
    auto denied = wrong.fetchIndex();
    ASSERT_FALSE(denied.ok());
    EXPECT_EQ(denied.error().code, ErrorCode::AuthenticationFailed) << denied.error().describe();
    EXPECT_NE(denied.error().message.find("STATUS_LOGON_FAILURE (0xC000006D): wrong user name or password"), std::string::npos)
        << denied.error().message;

    SmbClient smb = client("/HDD-Storage1/shop/nope.json");
    auto missing = smb.fetchIndex();
    ASSERT_FALSE(missing.ok());
    EXPECT_EQ(missing.error().code, ErrorCode::NotFound) << missing.error().describe();

    SmbClient noShare = client("/NoSuchShare/index.json");
    auto share = noShare.fetchIndex();
    ASSERT_FALSE(share.ok());
    EXPECT_EQ(share.error().code, ErrorCode::NotFound) << share.error().describe();
    EXPECT_NE(share.error().message.find("NoSuchShare"), std::string::npos) << share.error().message;
    EXPECT_NE(share.error().message.find("STATUS_BAD_NETWORK_NAME"), std::string::npos) << share.error().message;

    // Guest on a share reserved to a user.
    SmbClient guest = client("/HDD-Storage1/shop/index.json", "", "");
    auto refused = guest.fetchIndex();
    ASSERT_FALSE(refused.ok());
    EXPECT_EQ(refused.error().code, ErrorCode::PermissionDenied) << refused.error().describe();

    auto config = smbConfigFromUrl("smb://127.0.0.1:1/HDD-Storage1/index.json", "retro", "manager");
    config.value().timeoutSeconds = 3;
    auto unreachable = SmbClient(config.value()).fetchIndex();
    ASSERT_FALSE(unreachable.ok());
    EXPECT_EQ(unreachable.error().code, ErrorCode::NetworkError) << unreachable.error().describe();
    EXPECT_NE(unreachable.error().message.find("connection refused"), std::string::npos) << unreachable.error().message;
}

TEST_F(Smb, NeverSendsCredentialsToAnotherShare) {
    SmbClient smb = client("/HDD-Storage1/index.json");
    auto other = get(smb, root + "/Public/hello.txt");
    ASSERT_FALSE(other.ok());
    EXPECT_EQ(other.error().code, ErrorCode::PermissionDenied);
}

TEST_F(Smb, UploadsCreateFoldersListAndReplaceAtomically) {
    const std::string dir = root + "/Saves/run-" + std::to_string(std::rand()) + "/sub dir/";
    SmbClient smb = client("/Saves/index.json");
    ASSERT_TRUE(put(smb, dir + "Mario.srm", "first").ok());
    ASSERT_TRUE(put(smb, dir + "Mario.srm", "second version").ok());
    auto listing = smb.listDirectory(dir);
    ASSERT_TRUE(listing.ok()) << listing.error().describe();
    ASSERT_EQ(listing.value().size(), 1u);  // no leftover .tmp
    EXPECT_EQ(listing.value()[0].name, "Mario.srm");
    EXPECT_EQ(listing.value()[0].size, 14u);
    EXPECT_FALSE(listing.value()[0].isDirectory);
    EXPECT_TRUE(listing.value()[0].modifiedAt.has_value());
    EXPECT_EQ(get(smb, dir + "Mario.srm").value(), "second version");

    CancellationToken cancel;
    cancel.cancel();
    EXPECT_EQ(put(smb, dir + "Zelda.srm", "never", &cancel).error().code, ErrorCode::Cancelled);
    EXPECT_EQ(smb.listDirectory(dir).value().size(), 1u);
    EXPECT_EQ(smb.listDirectory(root + "/Saves/does-not-exist/").error().code, ErrorCode::NotFound);
}
