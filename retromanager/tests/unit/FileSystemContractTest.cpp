// Behavioural contract of IFileSystem.
//
// Every implementation runs this exact suite. It is what guarantees that code
// tested against the in-memory mock behaves the same on a real SD card.

#include <gtest/gtest.h>

#include <ctime>
#include <memory>

#include "MemoryFileSystem.hpp"
#include "TempDir.hpp"
#include "retromanager/platform/IFileSystem.hpp"
#include "retromanager/platform/LocalFileSystem.hpp"

using namespace rm;

namespace {

struct MemoryFsFactory {
    std::unique_ptr<IFileSystem> create() { return std::make_unique<test::MemoryFileSystem>(); }
};

struct LocalFsFactory {
    test::TempDir dir;
    std::unique_ptr<IFileSystem> create() { return std::make_unique<LocalFileSystem>(dir.path()); }
};

template <typename Factory>
class FileSystemContract : public ::testing::Test {
  protected:
    Factory factory;
    std::unique_ptr<IFileSystem> fsPtr = factory.create();
    IFileSystem& fs = *fsPtr;

    void write(std::string_view path, std::string_view content) {
        Status status = fs.writeFile(path, content);
        ASSERT_TRUE(status.ok()) << path << ": " << status.error().describe();
    }

    void mkdirs(std::string_view path) {
        Status status = fs.createDirectories(path);
        ASSERT_TRUE(status.ok()) << path << ": " << status.error().describe();
    }
};

using Implementations = ::testing::Types<MemoryFsFactory, LocalFsFactory>;

class ImplementationNames {
  public:
    template <typename T>
    static std::string GetName(int) {
        if (std::is_same_v<T, MemoryFsFactory>) return "Memory";
        if (std::is_same_v<T, LocalFsFactory>) return "Local";
        return "Unknown";
    }
};

TYPED_TEST_SUITE(FileSystemContract, Implementations, ImplementationNames);

// --- stat / exists -------------------------------------------------------

TYPED_TEST(FileSystemContract, RootIsAnEmptyDirectory) {
    auto info = this->fs.stat("/");
    ASSERT_TRUE(info.ok());
    EXPECT_EQ(info.value().type, EntryType::Directory);

    auto entries = this->fs.listDirectory("/");
    ASSERT_TRUE(entries.ok());
    EXPECT_TRUE(entries.value().empty());
}

TYPED_TEST(FileSystemContract, StatReportsTypeAndSize) {
    this->mkdirs("/retroarch");
    this->write("/retroarch/retroarch.cfg", "video_vsync = \"true\"\n");

    auto file = this->fs.stat("/retroarch/retroarch.cfg");
    ASSERT_TRUE(file.ok());
    EXPECT_EQ(file.value().type, EntryType::File);
    EXPECT_EQ(file.value().size, 21u);

    EXPECT_TRUE(this->fs.isDirectory("/retroarch"));
    EXPECT_TRUE(this->fs.isFile("/retroarch/retroarch.cfg"));
    EXPECT_FALSE(this->fs.isFile("/retroarch"));
}

TYPED_TEST(FileSystemContract, StatMissingPathIsNotFound) {
    auto info = this->fs.stat("/nope");
    ASSERT_FALSE(info.ok());
    EXPECT_EQ(info.error().code, ErrorCode::NotFound);
    EXPECT_FALSE(this->fs.exists("/nope"));
}

// --- read / write --------------------------------------------------------

TYPED_TEST(FileSystemContract, WriteThenReadRoundTripsBinaryData) {
    static constexpr char kRaw[] = "SRM\0\x01\xff save data";
    const std::string payload(kRaw, sizeof(kRaw) - 1);  // keeps the embedded NUL
    ASSERT_EQ(payload.size(), 16u);
    this->write("/game.srm", payload);

    auto content = this->fs.readFile("/game.srm");
    ASSERT_TRUE(content.ok());
    EXPECT_EQ(content.value(), payload);
}

TYPED_TEST(FileSystemContract, WriteEmptyFile) {
    this->write("/empty.txt", "");
    auto info = this->fs.stat("/empty.txt");
    ASSERT_TRUE(info.ok());
    EXPECT_EQ(info.value().size, 0u);
}

TYPED_TEST(FileSystemContract, WriteReplacesExistingContent) {
    this->write("/a.cfg", "a long original content");
    this->write("/a.cfg", "short");
    EXPECT_EQ(this->fs.readFile("/a.cfg").value(), "short");
}

TYPED_TEST(FileSystemContract, ReadIsChunkedAndEndsWithZero) {
    this->write("/f.bin", "0123456789");
    auto stream = this->fs.openRead("/f.bin");
    ASSERT_TRUE(stream.ok());

    char buffer[4];
    std::string collected;
    while (true) {
        auto count = stream.value()->read(buffer, sizeof(buffer));
        ASSERT_TRUE(count.ok());
        if (count.value() == 0) break;
        EXPECT_LE(count.value(), sizeof(buffer));
        collected.append(buffer, count.value());
    }
    EXPECT_EQ(collected, "0123456789");
}

TYPED_TEST(FileSystemContract, WriteIsInvisibleUntilClose) {
    auto stream = this->fs.openWrite("/download.zip");
    ASSERT_TRUE(stream.ok());
    ASSERT_TRUE(stream.value()->write("partial", 7).ok());

    EXPECT_FALSE(this->fs.exists("/download.zip"));
    EXPECT_TRUE(this->fs.listDirectory("/").value().empty());

    ASSERT_TRUE(stream.value()->close().ok());
    EXPECT_EQ(this->fs.readFile("/download.zip").value(), "partial");
}

TYPED_TEST(FileSystemContract, AbandonedWriteKeepsPreviousContent) {
    this->write("/save.srm", "good save");
    {
        auto stream = this->fs.openWrite("/save.srm");
        ASSERT_TRUE(stream.ok());
        ASSERT_TRUE(stream.value()->write("corrupt", 7).ok());
        // destroyed without close(): simulates a crash mid-sync
    }
    EXPECT_EQ(this->fs.readFile("/save.srm").value(), "good save");
    EXPECT_EQ(this->fs.listDirectory("/").value().size(), 1u);
}

// --- resumable writes (interrupted downloads) ------------------------------

TYPED_TEST(FileSystemContract, SuspendedWriteKeepsItsBytesForAResume) {
    this->mkdirs("/roms/nds");
    {
        auto stream = this->fs.openWrite("/roms/nds/Game.nds");
        ASSERT_TRUE(stream.ok());
        EXPECT_EQ(stream.value()->resumedFrom(), 0u);
        ASSERT_TRUE(stream.value()->write("hello", 5).ok());
        ASSERT_TRUE(stream.value()->suspend().ok());  // connection lost: keep what we have
    }
    EXPECT_FALSE(this->fs.exists("/roms/nds/Game.nds"));            // still not published
    EXPECT_TRUE(this->fs.listDirectory("/roms/nds").value().empty());  // staging stays hidden
    auto partial = this->fs.stat(stagingPath("/roms/nds/Game.nds"));
    ASSERT_TRUE(partial.ok()) << partial.error().describe();
    EXPECT_EQ(partial.value().size, 5u);
    EXPECT_EQ(this->fs.readFile(stagingPath("/roms/nds/Game.nds")).value(), "hello");

    auto resumed = this->fs.openWrite("/roms/nds/Game.nds", WriteOptions{true});
    ASSERT_TRUE(resumed.ok());
    EXPECT_EQ(resumed.value()->resumedFrom(), 5u);
    ASSERT_TRUE(resumed.value()->write(" world", 6).ok());
    ASSERT_TRUE(resumed.value()->close().ok());
    EXPECT_EQ(this->fs.readFile("/roms/nds/Game.nds").value(), "hello world");
    EXPECT_FALSE(this->fs.exists(stagingPath("/roms/nds/Game.nds")));
}

TYPED_TEST(FileSystemContract, ResumeWithoutPartialFileStartsEmpty) {
    auto stream = this->fs.openWrite("/a.bin", WriteOptions{true});
    ASSERT_TRUE(stream.ok());
    EXPECT_EQ(stream.value()->resumedFrom(), 0u);
    ASSERT_TRUE(stream.value()->write("x", 1).ok());
    ASSERT_TRUE(stream.value()->close().ok());
    EXPECT_EQ(this->fs.readFile("/a.bin").value(), "x");
}

TYPED_TEST(FileSystemContract, AFreshWriteDiscardsAnOldPartialFile) {
    {
        auto stream = this->fs.openWrite("/a.bin");
        ASSERT_TRUE(stream.value()->write("stale", 5).ok());
        ASSERT_TRUE(stream.value()->suspend().ok());
    }
    auto fresh = this->fs.openWrite("/a.bin");
    ASSERT_TRUE(fresh.ok());
    EXPECT_EQ(fresh.value()->resumedFrom(), 0u);
    ASSERT_TRUE(fresh.value()->write("new", 3).ok());
    ASSERT_TRUE(fresh.value()->close().ok());
    EXPECT_EQ(this->fs.readFile("/a.bin").value(), "new");
}

TYPED_TEST(FileSystemContract, AbandoningAResumedWriteDiscardsThePartialFile) {
    this->write("/a.bin", "previous version");
    {
        auto stream = this->fs.openWrite("/a.bin");
        ASSERT_TRUE(stream.value()->write("part", 4).ok());
        ASSERT_TRUE(stream.value()->suspend().ok());
    }
    {
        auto resumed = this->fs.openWrite("/a.bin", WriteOptions{true});
        ASSERT_TRUE(resumed.ok());
        ASSERT_TRUE(resumed.value()->write("more", 4).ok());
        // destroyed without close() nor suspend(): the user cancelled
    }
    EXPECT_FALSE(this->fs.exists(stagingPath("/a.bin")));
    EXPECT_EQ(this->fs.readFile("/a.bin").value(), "previous version");
}

TYPED_TEST(FileSystemContract, ASuspendedStreamIsClosed) {
    auto stream = this->fs.openWrite("/a.bin");
    ASSERT_TRUE(stream.value()->suspend().ok());
    EXPECT_FALSE(stream.value()->write("x", 1).ok());
    EXPECT_FALSE(stream.value()->close().ok());
    EXPECT_FALSE(stream.value()->suspend().ok());
}

TYPED_TEST(FileSystemContract, WriteRequiresExistingParent) {
    auto stream = this->fs.openWrite("/missing/dir/file.txt");
    ASSERT_FALSE(stream.ok());
    EXPECT_EQ(stream.error().code, ErrorCode::NotFound);
}

TYPED_TEST(FileSystemContract, WriteUnderAFileIsNotADirectory) {
    this->write("/file", "x");
    auto stream = this->fs.openWrite("/file/child.txt");
    ASSERT_FALSE(stream.ok());
    EXPECT_EQ(stream.error().code, ErrorCode::NotADirectory);
}

TYPED_TEST(FileSystemContract, WriteOverADirectoryFails) {
    this->mkdirs("/roms");
    auto stream = this->fs.openWrite("/roms");
    ASSERT_FALSE(stream.ok());
    EXPECT_EQ(stream.error().code, ErrorCode::IsADirectory);
}

TYPED_TEST(FileSystemContract, ReadErrors) {
    this->mkdirs("/roms");

    auto missing = this->fs.openRead("/nope.sfc");
    ASSERT_FALSE(missing.ok());
    EXPECT_EQ(missing.error().code, ErrorCode::NotFound);

    auto directory = this->fs.openRead("/roms");
    ASSERT_FALSE(directory.ok());
    EXPECT_EQ(directory.error().code, ErrorCode::IsADirectory);
}

// --- directories ---------------------------------------------------------

TYPED_TEST(FileSystemContract, CreateDirectoriesIsRecursiveAndIdempotent) {
    this->mkdirs("/retroarch/cores/info");
    this->mkdirs("/retroarch/cores/info");
    EXPECT_TRUE(this->fs.isDirectory("/retroarch"));
    EXPECT_TRUE(this->fs.isDirectory("/retroarch/cores"));
    EXPECT_TRUE(this->fs.isDirectory("/retroarch/cores/info"));
}

TYPED_TEST(FileSystemContract, CreateDirectoriesThroughAFileFails) {
    this->write("/file", "x");
    Status status = this->fs.createDirectories("/file/sub");
    ASSERT_FALSE(status.ok());
    EXPECT_EQ(status.error().code, ErrorCode::NotADirectory);

    Status same = this->fs.createDirectories("/file");
    ASSERT_FALSE(same.ok());
    EXPECT_EQ(same.error().code, ErrorCode::NotADirectory);
}

TYPED_TEST(FileSystemContract, ListDirectoryIsSortedWithTypesAndSizes) {
    this->mkdirs("/roms/snes");
    this->write("/roms/zelda.sfc", "1234");
    this->write("/roms/Aladdin.sfc", "12");
    this->mkdirs("/roms/snes/nested");  // grand-children must not be listed

    auto entries = this->fs.listDirectory("/roms");
    ASSERT_TRUE(entries.ok());
    std::vector<DirEntry> expected = {
        {"Aladdin.sfc", EntryType::File, 2},
        {"snes", EntryType::Directory, 0},
        {"zelda.sfc", EntryType::File, 4},
    };
    EXPECT_EQ(entries.value(), expected);
}

TYPED_TEST(FileSystemContract, ListDirectoryErrors) {
    this->write("/file", "x");

    auto missing = this->fs.listDirectory("/nope");
    ASSERT_FALSE(missing.ok());
    EXPECT_EQ(missing.error().code, ErrorCode::NotFound);

    auto file = this->fs.listDirectory("/file");
    ASSERT_FALSE(file.ok());
    EXPECT_EQ(file.error().code, ErrorCode::NotADirectory);
}

TYPED_TEST(FileSystemContract, SiblingsSharingAPrefixAreIndependent) {
    // "/roms-old" sorts between "/roms" and "/roms/..." in byte order.
    this->mkdirs("/roms/snes");
    this->mkdirs("/roms-old");
    this->write("/roms-old/keep.sfc", "k");
    this->write("/roms/snes/a.sfc", "a");

    auto listed = this->fs.listDirectory("/roms");
    ASSERT_TRUE(listed.ok());
    ASSERT_EQ(listed.value().size(), 1u);
    EXPECT_EQ(listed.value()[0].name, "snes");

    ASSERT_TRUE(this->fs.rename("/roms", "/roms2").ok());
    EXPECT_TRUE(this->fs.isFile("/roms2/snes/a.sfc"));
    EXPECT_TRUE(this->fs.isFile("/roms-old/keep.sfc"));

    ASSERT_TRUE(this->fs.removeAll("/roms2").ok());
    EXPECT_TRUE(this->fs.isFile("/roms-old/keep.sfc"));
}

// --- remove --------------------------------------------------------------

TYPED_TEST(FileSystemContract, RemoveFileAndEmptyDirectory) {
    this->mkdirs("/empty");
    this->write("/file", "x");

    ASSERT_TRUE(this->fs.remove("/file").ok());
    ASSERT_TRUE(this->fs.remove("/empty").ok());
    EXPECT_FALSE(this->fs.exists("/file"));
    EXPECT_FALSE(this->fs.exists("/empty"));
}

TYPED_TEST(FileSystemContract, RemoveErrors) {
    this->mkdirs("/full");
    this->write("/full/file", "x");

    Status notEmpty = this->fs.remove("/full");
    ASSERT_FALSE(notEmpty.ok());
    EXPECT_EQ(notEmpty.error().code, ErrorCode::NotEmpty);

    Status missing = this->fs.remove("/nope");
    ASSERT_FALSE(missing.ok());
    EXPECT_EQ(missing.error().code, ErrorCode::NotFound);
}

TYPED_TEST(FileSystemContract, RemoveAllDeletesTreesAndToleratesMissing) {
    this->mkdirs("/roms/snes/sub");
    this->write("/roms/snes/zelda.sfc", "x");
    this->write("/roms/snes/sub/a", "y");
    this->write("/keep.txt", "z");

    ASSERT_TRUE(this->fs.removeAll("/roms").ok());
    EXPECT_FALSE(this->fs.exists("/roms"));
    EXPECT_FALSE(this->fs.exists("/roms/snes/zelda.sfc"));
    EXPECT_TRUE(this->fs.exists("/keep.txt"));

    EXPECT_TRUE(this->fs.removeAll("/roms").ok());
}

TYPED_TEST(FileSystemContract, RemoveRootIsRejected) {
    Status status = this->fs.removeAll("/");
    ASSERT_FALSE(status.ok());
    EXPECT_EQ(status.error().code, ErrorCode::InvalidPath);
}

// --- rename --------------------------------------------------------------

TYPED_TEST(FileSystemContract, RenameMovesFile) {
    this->mkdirs("/archive");
    this->write("/zelda.sfc", "rom");

    ASSERT_TRUE(this->fs.rename("/zelda.sfc", "/archive/zelda.sfc").ok());
    EXPECT_FALSE(this->fs.exists("/zelda.sfc"));
    EXPECT_EQ(this->fs.readFile("/archive/zelda.sfc").value(), "rom");
}

TYPED_TEST(FileSystemContract, RenameReplacesDestinationFile) {
    this->write("/new.srm", "new");
    this->write("/old.srm", "old");

    ASSERT_TRUE(this->fs.rename("/new.srm", "/old.srm").ok());
    EXPECT_EQ(this->fs.readFile("/old.srm").value(), "new");
    EXPECT_FALSE(this->fs.exists("/new.srm"));
}

TYPED_TEST(FileSystemContract, RenameMovesDirectoryTree) {
    this->mkdirs("/a/b");
    this->write("/a/b/c.txt", "deep");

    ASSERT_TRUE(this->fs.rename("/a", "/z").ok());
    EXPECT_FALSE(this->fs.exists("/a"));
    EXPECT_EQ(this->fs.readFile("/z/b/c.txt").value(), "deep");
}

TYPED_TEST(FileSystemContract, RenameErrors) {
    this->write("/file", "x");

    Status missing = this->fs.rename("/nope", "/other");
    ASSERT_FALSE(missing.ok());
    EXPECT_EQ(missing.error().code, ErrorCode::NotFound);

    Status noParent = this->fs.rename("/file", "/missing/file");
    ASSERT_FALSE(noParent.ok());
    EXPECT_EQ(noParent.error().code, ErrorCode::NotFound);
    EXPECT_TRUE(this->fs.exists("/file"));
}

// --- modification time ---------------------------------------------------

TYPED_TEST(FileSystemContract, FilesCarryTheirModificationTime) {
    const std::int64_t before = static_cast<std::int64_t>(std::time(nullptr));
    this->mkdirs("/saves");
    this->write("/saves/Game.srm", "save");
    const std::int64_t after = static_cast<std::int64_t>(std::time(nullptr));

    auto info = this->fs.stat("/saves/Game.srm");
    ASSERT_TRUE(info.ok());
    // Seconds since the Unix epoch, UTC. FAT only stores 2 s steps.
    EXPECT_GE(info.value().modifiedAt, before - 2);
    EXPECT_LE(info.value().modifiedAt, after + 2);

    auto listed = this->fs.listDirectory("/saves");
    ASSERT_TRUE(listed.ok());
    ASSERT_EQ(listed.value().size(), 1u);
    EXPECT_EQ(listed.value()[0].modifiedAt, info.value().modifiedAt);
}

TYPED_TEST(FileSystemContract, RenameKeepsTheModificationTime) {
    this->write("/a.srm", "x");
    std::int64_t original = this->fs.stat("/a.srm").value().modifiedAt;
    ASSERT_TRUE(this->fs.rename("/a.srm", "/b.srm").ok());
    EXPECT_EQ(this->fs.stat("/b.srm").value().modifiedAt, original);
}

// --- free space ----------------------------------------------------------

TYPED_TEST(FileSystemContract, AvailableSpaceOnExistingPaths) {
    this->mkdirs("/roms");
    this->write("/roms/a.sfc", "x");

    auto root = this->fs.availableSpace("/");
    ASSERT_TRUE(root.ok()) << root.error().describe();
    EXPECT_GT(root.value(), 0u);
    EXPECT_TRUE(this->fs.availableSpace("/roms").ok());
    EXPECT_TRUE(this->fs.availableSpace("/roms/a.sfc").ok());
}

TYPED_TEST(FileSystemContract, AvailableSpaceErrors) {
    EXPECT_EQ(this->fs.availableSpace("/missing").error().code, ErrorCode::NotFound);
    EXPECT_EQ(this->fs.availableSpace("relative").error().code, ErrorCode::InvalidPath);
}

TYPED_TEST(FileSystemContract, StagingFilesNeverShowUpInListings) {
    this->mkdirs("/roms/nds");
    auto stream = this->fs.openWrite("/roms/nds/Game.nds");
    ASSERT_TRUE(stream.ok());
    ASSERT_TRUE(stream.value()->write("x", 1).ok());
    EXPECT_TRUE(this->fs.listDirectory("/roms/nds").value().empty());
    ASSERT_TRUE(stream.value()->close().ok());

    auto entries = this->fs.listDirectory("/roms/nds").value();
    ASSERT_EQ(entries.size(), 1u);
    EXPECT_EQ(entries[0].name, "Game.nds");
}

// --- path validation -----------------------------------------------------

TYPED_TEST(FileSystemContract, EveryOperationRejectsInvalidPaths) {
    for (std::string_view bad : {"relative/path", "/../escape", ""}) {
        SCOPED_TRACE(std::string(bad));
        EXPECT_EQ(this->fs.stat(bad).error().code, ErrorCode::InvalidPath);
        EXPECT_EQ(this->fs.listDirectory(bad).error().code, ErrorCode::InvalidPath);
        EXPECT_EQ(this->fs.createDirectories(bad).error().code, ErrorCode::InvalidPath);
        EXPECT_EQ(this->fs.openRead(bad).error().code, ErrorCode::InvalidPath);
        EXPECT_EQ(this->fs.openWrite(bad).error().code, ErrorCode::InvalidPath);
        EXPECT_EQ(this->fs.remove(bad).error().code, ErrorCode::InvalidPath);
        EXPECT_EQ(this->fs.removeAll(bad).error().code, ErrorCode::InvalidPath);
        EXPECT_EQ(this->fs.rename(bad, "/x").error().code, ErrorCode::InvalidPath);
        EXPECT_EQ(this->fs.rename("/x", bad).error().code, ErrorCode::InvalidPath);
    }
}

TYPED_TEST(FileSystemContract, EquivalentSpellingsReachTheSameFile) {
    this->mkdirs("/retroarch");
    this->write("/retroarch/./retroarch.cfg", "x");
    EXPECT_TRUE(this->fs.isFile("//retroarch/../retroarch/retroarch.cfg"));
    EXPECT_TRUE(this->fs.isFile("\\retroarch\\retroarch.cfg"));
}

}  // namespace
