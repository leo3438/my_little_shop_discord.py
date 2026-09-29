#include <gtest/gtest.h>

#include "MemoryFileSystem.hpp"
#include "retromanager/core/Crc32.hpp"
#include "retromanager/fs/RomStore.hpp"

using namespace rm;

namespace {

GameEntry game(std::string fileName, std::string system = "nds", std::uint64_t size = 0) {
    GameEntry entry;
    entry.id = system + "/" + fileName;
    entry.title = fileName;
    entry.system = std::move(system);
    entry.fileName = std::move(fileName);
    entry.sizeBytes = size;
    return entry;
}

std::string install(RomStore& store, const GameEntry& entry, const std::string& content) {
    auto started = store.beginInstall(entry);
    EXPECT_TRUE(started.ok()) << started.error().describe();
    if (!started.ok()) return "";
    EXPECT_TRUE(started.value()->write(content.data(), content.size()).ok());
    Status committed = started.value()->commit();
    EXPECT_TRUE(committed.ok()) << committed.error().describe();
    return started.value()->destination();
}

}  // namespace

// --- destinations --------------------------------------------------------

TEST(RomStore, DestinationIsRomsSystemFileName) {
    test::MemoryFileSystem fs;
    RomStore store(fs, SdLayout{});
    EXPECT_EQ(store.destinationFor(game("Pokemon Platine (France).nds")).value(), "/roms/nds/Pokemon Platine (France).nds");
}

TEST(RomStore, SanitizesNamesForFatFileSystems) {
    test::MemoryFileSystem fs;
    RomStore store(fs, SdLayout{});
    EXPECT_EQ(store.destinationFor(game("Zelda: A Link <USA>?.sfc", "snes")).value(), "/roms/snes/Zelda_ A Link _USA__.sfc");
    EXPECT_EQ(store.destinationFor(game("Tab\there|\"x\".gb", "gb")).value(), "/roms/gb/Tab_here__x_.gb");
    EXPECT_EQ(store.destinationFor(game("Trailing dots... ", "gb")).value(), "/roms/gb/Trailing dots");
}

TEST(RomStore, RejectsUnusableNames) {
    test::MemoryFileSystem fs;
    RomStore store(fs, SdLayout{});
    for (const char* bad : {"", "..", ".", "   ", "...", ".hidden.nds", "a/b.nds", "a\\b.nds"}) {
        auto destination = store.destinationFor(game(bad));
        EXPECT_FALSE(destination.ok()) << '"' << bad << "\" -> " << destination.valueOr("");
    }
    EXPECT_FALSE(store.destinationFor(game("ok.nds", "../etc")).ok());
    EXPECT_FALSE(store.destinationFor(game("ok.nds", "")).ok());
    EXPECT_FALSE(store.destinationFor(game("ok.nds", "NDS")).ok());  // ids are lowercase by contract
    EXPECT_FALSE(store.destinationFor(game(std::string(300, 'a') + ".nds")).ok());
}

// --- free space ----------------------------------------------------------

TEST(RomStore, RefusesToStartWhenTheCardIsTooFull) {
    test::MemoryFileSystem fs;
    fs.setCapacity(100 * 1024 * 1024);  // 100 MiB card
    RomStore store(fs, SdLayout{});

    auto started = store.beginInstall(game("Big.nds", "nds", 128ull * 1024 * 1024));
    ASSERT_FALSE(started.ok());
    EXPECT_EQ(started.error().code, ErrorCode::InsufficientSpace);
    EXPECT_NE(started.error().message.find("128 MB"), std::string::npos) << started.error().message;
    EXPECT_NE(started.error().message.find("100 MB"), std::string::npos) << started.error().message;
    EXPECT_EQ(fs.nodeCount(), 0u);  // nothing created on the card

    SpaceReport report = store.spaceReport(game("Big.nds", "nds", 128ull * 1024 * 1024));
    EXPECT_FALSE(report.sufficient);
    EXPECT_EQ(report.requiredBytes, 128ull * 1024 * 1024 + RomStore::kSpaceMargin);
    EXPECT_EQ(report.availableBytes, 100ull * 1024 * 1024);
}

TEST(RomStore, KeepsASafetyMargin) {
    test::MemoryFileSystem fs;
    fs.setCapacity(10 * 1024 * 1024);
    RomStore store(fs, SdLayout{});
    // Exactly the free space is not enough: the margin must fit too.
    EXPECT_FALSE(store.spaceReport(game("a.nds", "nds", 10 * 1024 * 1024)).sufficient);
    EXPECT_TRUE(store.spaceReport(game("a.nds", "nds", 9 * 1024 * 1024)).sufficient);
}

TEST(RomStore, UnknownSizeOrUnknownFreeSpaceDoesNotBlock) {
    test::MemoryFileSystem fs;
    fs.setCapacity(1024);
    RomStore store(fs, SdLayout{});
    EXPECT_TRUE(store.spaceReport(game("a.nds", "nds", 0)).sufficient);  // size not in the index

    fs.setCapacity(std::nullopt);  // platform cannot tell
    SpaceReport report = store.spaceReport(game("a.nds", "nds", 1ull << 40));
    EXPECT_TRUE(report.sufficient);
    EXPECT_FALSE(report.availableBytes.has_value());
}

// --- installs ------------------------------------------------------------

TEST(RomStore, CommitPublishesTheFileAtomically) {
    test::MemoryFileSystem fs;
    RomStore store(fs, SdLayout{});
    GameEntry entry = game("Game.nds");

    auto started = store.beginInstall(entry);
    ASSERT_TRUE(started.ok());
    ASSERT_TRUE(started.value()->write("ROM", 3).ok());
    EXPECT_FALSE(store.isInstalled(entry));  // not visible before commit
    ASSERT_TRUE(started.value()->commit().ok());

    EXPECT_TRUE(store.isInstalled(entry));
    EXPECT_EQ(fs.readFile("/roms/nds/Game.nds").value(), "ROM");
    EXPECT_EQ(fs.listDirectory("/roms/nds").value().size(), 1u);
    EXPECT_EQ(started.value()->bytesWritten(), 3u);
}

TEST(RomStore, AbandonedInstallLeavesNothingAndKeepsThePreviousVersion) {
    test::MemoryFileSystem fs;
    RomStore store(fs, SdLayout{});
    GameEntry entry = game("Game.nds");
    install(store, entry, "version 1");

    {
        auto started = store.beginInstall(entry);
        ASSERT_TRUE(started.ok());
        ASSERT_TRUE(started.value()->write("half of version 2", 17).ok());
    }  // cancelled: destroyed without commit

    EXPECT_EQ(fs.readFile("/roms/nds/Game.nds").value(), "version 1");
    EXPECT_EQ(fs.listDirectory("/roms/nds").value().size(), 1u);
}

TEST(RomStore, VerifiesTheAnnouncedCrc) {
    test::MemoryFileSystem fs;
    RomStore store(fs, SdLayout{});
    const std::string content = "123456789";

    GameEntry good = game("Good.nds");
    good.crc32 = "cbf43926";
    EXPECT_EQ(install(store, good, content), "/roms/nds/Good.nds");

    GameEntry corrupt = game("Corrupt.nds");
    corrupt.crc32 = "deadbeef";
    auto started = store.beginInstall(corrupt);
    ASSERT_TRUE(started.ok());
    ASSERT_TRUE(started.value()->write(content.data(), content.size()).ok());
    Status committed = started.value()->commit();
    ASSERT_FALSE(committed.ok());
    EXPECT_EQ(committed.error().code, ErrorCode::IntegrityError);
    EXPECT_NE(committed.error().message.find("cbf43926"), std::string::npos) << committed.error().message;
    started.value().reset();
    EXPECT_FALSE(fs.exists("/roms/nds/Corrupt.nds"));
}

TEST(RomStore, WritesReachTheCardInBoundedChunks) {
    // Spy filesystem: records the size of every write that reaches "the card".
    class SpyFs : public test::MemoryFileSystem {
      public:
        std::size_t maxWrite = 0;
        std::size_t writes = 0;
        Result<std::unique_ptr<IWriteStream>> openWrite(std::string_view path, WriteOptions options = {}) override {
            auto inner = MemoryFileSystem::openWrite(path, options);
            if (!inner) return inner.error();
            class Spy : public IWriteStream {
              public:
                Spy(std::unique_ptr<IWriteStream> in, SpyFs& owner) : in_(std::move(in)), owner_(owner) {}
                Status write(const char* d, std::size_t n) override {
                    owner_.maxWrite = std::max(owner_.maxWrite, n);
                    ++owner_.writes;
                    return in_->write(d, n);
                }
                Status close() override { return in_->close(); }
                Status suspend() override { return in_->suspend(); }

              private:
                std::unique_ptr<IWriteStream> in_;
                SpyFs& owner_;
            };
            return std::unique_ptr<IWriteStream>(std::make_unique<Spy>(std::move(inner.value()), *this));
        }
    } fs;
    RomStore store(fs, SdLayout{});

    auto started = store.beginInstall(game("Big.nds"));
    ASSERT_TRUE(started.ok());
    std::string chunk(16 * 1024, 'x');  // network-sized chunks
    for (int i = 0; i < 320; ++i) ASSERT_TRUE(started.value()->write(chunk.data(), chunk.size()).ok());  // 5 MiB
    ASSERT_TRUE(started.value()->commit().ok());

    EXPECT_EQ(fs.maxWrite, BufferedWriteStream::kDefaultCapacity);
    EXPECT_EQ(fs.writes, 5u);  // 5 MiB / 1 MiB
}
