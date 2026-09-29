// Remote listing and upload: the in-memory mock, and the pure parts of the
// FTP client (MLSD parsing). The FTP transfers themselves are covered by
// tests/integration/FtpSaveIntegrationTest.cpp.

#include <gtest/gtest.h>

#include "retromanager/network/MockRemoteSource.hpp"

using namespace rm;

namespace {

const std::string kSaves = "ftp://mock.local/Saves/";

// Serves `data` in chunks of at most `chunk` bytes.
ChunkReader readerOf(const std::string& data, std::size_t chunk = 7, std::size_t* calls = nullptr) {
    auto offset = std::make_shared<std::size_t>(0);
    return [data, chunk, offset, calls](char* buffer, std::size_t capacity) -> Result<std::size_t> {
        if (calls != nullptr) ++*calls;
        std::size_t n = std::min({chunk, capacity, data.size() - *offset});
        std::copy_n(data.data() + *offset, n, buffer);
        *offset += n;
        return n;
    };
}

}  // namespace

// --- MockRemoteSource ----------------------------------------------------

TEST(MockRemoteFiles, UploadThenListAndDownload) {
    MockRemoteSource remote("{}", "");
    remote.setClock([] { return std::int64_t{1700000000}; });
    CancellationToken cancel;
    std::size_t calls = 0;

    ASSERT_TRUE(remote.uploadFile(kSaves + "Zelda%20(USA).srm", readerOf("SRAM-DATA", 4, &calls), 9, nullptr, cancel).ok());
    EXPECT_GE(calls, 3u);  // pulled in chunks

    auto listing = remote.listDirectory(kSaves);
    ASSERT_TRUE(listing.ok()) << listing.error().describe();
    ASSERT_EQ(listing.value().size(), 1u);
    EXPECT_EQ(listing.value()[0].name, "Zelda (USA).srm");
    EXPECT_FALSE(listing.value()[0].isDirectory);
    EXPECT_EQ(listing.value()[0].size, 9u);
    EXPECT_EQ(listing.value()[0].modifiedAt, 1700000000);

    std::string back;
    ASSERT_TRUE(remote
                    .downloadFile(
                        kSaves + "Zelda%20(USA).srm",
                        [&](const char* d, std::size_t n) {
                            back.append(d, n);
                            return success();
                        },
                        nullptr, cancel)
                    .ok());
    EXPECT_EQ(back, "SRAM-DATA");
}

TEST(MockRemoteFiles, ListsSubdirectories) {
    MockRemoteSource remote("{}", "");
    CancellationToken cancel;
    ASSERT_TRUE(remote.uploadFile(kSaves + "mGBA/Advance.srm", readerOf("a"), 1, nullptr, cancel).ok());
    ASSERT_TRUE(remote.uploadFile(kSaves + "root.sav", readerOf("b"), 1, nullptr, cancel).ok());

    auto root = remote.listDirectory(kSaves);
    ASSERT_TRUE(root.ok());
    ASSERT_EQ(root.value().size(), 2u);
    int dirs = 0;
    for (const RemoteEntry& e : root.value()) dirs += e.isDirectory && e.name == "mGBA" ? 1 : 0;
    EXPECT_EQ(dirs, 1);
    EXPECT_EQ(remote.listDirectory(kSaves + "mGBA/").value().size(), 1u);
    EXPECT_EQ(remote.listDirectory(kSaves + "mGBA").value().size(), 1u);  // trailing slash optional
}

TEST(MockRemoteFiles, MissingDirectoryIsNotFoundButRegisteredEmptyOneIsNot) {
    MockRemoteSource remote("{}", "");
    EXPECT_EQ(remote.listDirectory(kSaves).error().code, ErrorCode::NotFound);
    remote.addDirectory(kSaves);
    auto empty = remote.listDirectory(kSaves);
    ASSERT_TRUE(empty.ok());
    EXPECT_TRUE(empty.value().empty());
}

TEST(MockRemoteFiles, CancelledOrFailedUploadLeavesTheRemoteUntouched) {
    MockRemoteSource remote("{}", "");
    CancellationToken cancel;
    ASSERT_TRUE(remote.uploadFile(kSaves + "a.srm", readerOf("good"), 4, nullptr, cancel).ok());

    CancellationToken stop;
    auto cancelling = [&stop](char* buffer, std::size_t) -> Result<std::size_t> {
        stop.cancel();
        buffer[0] = 'X';
        return std::size_t{1};
    };
    EXPECT_EQ(remote.uploadFile(kSaves + "a.srm", cancelling, 10, nullptr, stop).error().code, ErrorCode::Cancelled);

    auto failing = [](char*, std::size_t) -> Result<std::size_t> { return makeError(ErrorCode::IoError, "SD read"); };
    EXPECT_EQ(remote.uploadFile(kSaves + "a.srm", failing, 10, nullptr, cancel).error().code, ErrorCode::IoError);

    EXPECT_EQ(remote.fileContent(kSaves + "a.srm"), "good");
}

TEST(MockRemoteFiles, UploadSizeMismatchIsAnError) {
    MockRemoteSource remote("{}", "");
    CancellationToken cancel;
    // The file shrank on the card between stat() and upload: refuse rather
    // than store something else than announced.
    EXPECT_EQ(remote.uploadFile(kSaves + "a.srm", readerOf("abc"), 10, nullptr, cancel).error().code, ErrorCode::IoError);
    EXPECT_FALSE(remote.fileContent(kSaves + "a.srm").has_value());
}

// --- MLSD parsing --------------------------------------------------------
