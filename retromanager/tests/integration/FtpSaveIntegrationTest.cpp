// Listing and streamed upload against a real FTP server
// (tools/test_ftp_server.py, writable /saves area). Skipped unless
// RM_TEST_FTP_PORT is set.

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <ctime>

#include "retromanager/network/FtpClient.hpp"

using namespace rm;

namespace {

ChunkReader readerOf(std::shared_ptr<const std::string> data, std::size_t* calls = nullptr) {
    auto offset = std::make_shared<std::size_t>(0);
    return [data, offset, calls](char* buffer, std::size_t capacity) -> Result<std::size_t> {
        if (calls != nullptr) ++*calls;
        std::size_t n = std::min(capacity, data->size() - *offset);
        std::copy_n(data->data() + *offset, n, buffer);
        *offset += n;
        return n;
    };
}

class FtpSaves : public ::testing::Test {
  protected:
    void SetUp() override {
        const char* port = std::getenv("RM_TEST_FTP_PORT");
        if (port == nullptr || *port == '\0') GTEST_SKIP() << "RM_TEST_FTP_PORT not set (see tools/test_ftp_server.py)";
        config.host = "127.0.0.1";
        config.port = static_cast<std::uint16_t>(std::atoi(port));
        config.username = "retro";
        config.password = "manager";
        config.connectTimeoutSeconds = 5;
        static std::atomic<int> counter{0};
        // One folder per test: runs never see each other's files.
        base = "ftp://127.0.0.1:" + std::string(port) + "/saves/" +
               ::testing::UnitTest::GetInstance()->current_test_info()->name() + "-" +
               std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" +
               std::to_string(counter++) + "/";
    }

    Status upload(FtpClient& client, const std::string& relative, const std::string& content) {
        CancellationToken cancel;
        return client.uploadFile(base + relative, readerOf(std::make_shared<const std::string>(content)), content.size(),
                                 nullptr, cancel);
    }

    std::string download(FtpClient& client, const std::string& relative) {
        std::string out;
        CancellationToken cancel;
        Status status = client.downloadFile(
            base + relative,
            [&](const char* d, std::size_t n) {
                out.append(d, n);
                return success();
            },
            nullptr, cancel);
        EXPECT_TRUE(status.ok()) << status.error().describe();
        return out;
    }

    FtpConfig config;
    std::string base;
};

const RemoteEntry* find(const std::vector<RemoteEntry>& entries, const std::string& name) {
    for (const RemoteEntry& e : entries) {
        if (e.name == name) return &e;
    }
    return nullptr;
}

}  // namespace

TEST_F(FtpSaves, UploadCreatesFoldersAndIsListedWithSizeAndTime) {
    FtpClient client(config);
    const std::int64_t before = static_cast<std::int64_t>(std::time(nullptr));
    ASSERT_TRUE(upload(client, "mGBA/Pokemon Emeraude (France).srm", "SRAM").ok());

    auto root = client.listDirectory(base);
    ASSERT_TRUE(root.ok()) << root.error().describe();
    const RemoteEntry* folder = find(root.value(), "mGBA");
    ASSERT_NE(folder, nullptr);
    EXPECT_TRUE(folder->isDirectory);

    auto listed = client.listDirectory(base + "mGBA/");
    ASSERT_TRUE(listed.ok()) << listed.error().describe();
    ASSERT_EQ(listed.value().size(), 1u);  // the hidden staging name is gone after the rename
    EXPECT_EQ(listed.value()[0].name, "Pokemon Emeraude (France).srm");
    EXPECT_EQ(listed.value()[0].size, 4u);
    ASSERT_TRUE(listed.value()[0].modifiedAt.has_value());
    EXPECT_GE(*listed.value()[0].modifiedAt, before - 5);
    EXPECT_EQ(download(client, "mGBA/Pokemon%20Emeraude%20(France).srm"), "SRAM");
}

TEST_F(FtpSaves, UploadReplacesAnExistingFile) {
    FtpClient client(config);
    ASSERT_TRUE(upload(client, "a.srm", "version 1, longer").ok());
    ASSERT_TRUE(upload(client, "a.srm", "v2").ok());
    EXPECT_EQ(download(client, "a.srm"), "v2");
}

TEST_F(FtpSaves, UploadStreamsInChunks) {
    FtpClient client(config);
    auto big = std::make_shared<const std::string>(4 * 1024 * 1024 + 17, 'S');
    std::size_t calls = 0;
    CancellationToken cancel;
    ASSERT_TRUE(client.uploadFile(base + "big.sav", readerOf(big, &calls), big->size(), nullptr, cancel).ok());
    EXPECT_GT(calls, 16u);  // pulled piecewise, never as one block
    auto listed = client.listDirectory(base);
    ASSERT_TRUE(listed.ok());
    EXPECT_EQ(find(listed.value(), "big.sav")->size, big->size());
}

TEST_F(FtpSaves, CancelledUploadNeverReplacesTheRemoteFile) {
    FtpClient client(config);
    ASSERT_TRUE(upload(client, "keep.srm", "good save").ok());

    CancellationToken cancel;
    std::size_t sent = 0;
    auto slow = [&](char* buffer, std::size_t capacity) -> Result<std::size_t> {
        std::size_t n = std::min<std::size_t>(capacity, 1024);
        std::fill_n(buffer, n, 'X');
        sent += n;
        if (sent > 64 * 1024) cancel.cancel();  // the user presses B mid-upload
        return n;
    };
    Status status = client.uploadFile(base + "keep.srm", slow, 10 * 1024 * 1024, nullptr, cancel);
    EXPECT_EQ(status.error().code, ErrorCode::Cancelled) << status.error().describe();
    EXPECT_EQ(download(client, "keep.srm"), "good save");
}

TEST_F(FtpSaves, ReaderErrorsAbortTheUpload) {
    FtpClient client(config);
    CancellationToken cancel;
    auto failing = [](char*, std::size_t) -> Result<std::size_t> { return makeError(ErrorCode::IoError, "SD card removed"); };
    Status status = client.uploadFile(base + "x.srm", failing, 100, nullptr, cancel);
    EXPECT_EQ(status.error().code, ErrorCode::IoError);
    EXPECT_EQ(status.error().message, "SD card removed");
}

TEST_F(FtpSaves, MissingDirectoryIsNotFound) {
    FtpClient client(config);
    auto listed = client.listDirectory(base + "does-not-exist/");
    ASSERT_FALSE(listed.ok());
    EXPECT_EQ(listed.error().code, ErrorCode::NotFound) << listed.error().describe();
}

TEST_F(FtpSaves, FallbackWithoutMlsdReportsTheSameInformation) {
    FtpClient client(config);
    ASSERT_TRUE(upload(client, "sub/inner.sav", "12345").ok());
    ASSERT_TRUE(upload(client, "top.srm", "123").ok());

    FtpConfig noMlsd = config;
    noMlsd.useMlsd = false;  // what a vsftpd server forces
    FtpClient fallback(noMlsd);
    auto listed = fallback.listDirectory(base);
    ASSERT_TRUE(listed.ok()) << listed.error().describe();
    auto mlsd = client.listDirectory(base);
    ASSERT_TRUE(mlsd.ok());

    const RemoteEntry* top = find(listed.value(), "top.srm");
    ASSERT_NE(top, nullptr);
    EXPECT_FALSE(top->isDirectory);
    EXPECT_EQ(top->size, 3u);
    ASSERT_TRUE(top->modifiedAt.has_value());
    EXPECT_EQ(top->modifiedAt, find(mlsd.value(), "top.srm")->modifiedAt);
    const RemoteEntry* sub = find(listed.value(), "sub");
    ASSERT_NE(sub, nullptr);
    EXPECT_TRUE(sub->isDirectory);
    EXPECT_EQ(fallback.listDirectory(base + "does-not-exist/").error().code, ErrorCode::NotFound);
}

TEST_F(FtpSaves, NeverUploadsToAnotherServer) {
    FtpClient client(config);
    CancellationToken cancel;
    auto data = std::make_shared<const std::string>("x");
    EXPECT_EQ(client.uploadFile("ftp://127.0.0.2:" + std::to_string(config.port) + "/saves/x", readerOf(data), 1, nullptr, cancel)
                  .error()
                  .code,
              ErrorCode::PermissionDenied);
    EXPECT_EQ(client.listDirectory("ftp://127.0.0.2:" + std::to_string(config.port) + "/saves/").error().code,
              ErrorCode::PermissionDenied);
}
