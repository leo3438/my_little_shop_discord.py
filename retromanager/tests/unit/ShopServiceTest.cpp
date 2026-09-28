#include <gtest/gtest.h>

#include <deque>
#include <optional>

#include "MemoryFileSystem.hpp"
#include "retromanager/network/MockRemoteSource.hpp"
#include "retromanager/services/ShopService.hpp"

using namespace rm;

namespace {

// Queues tasks so tests control exactly when "background" and "main thread"
// work happens.
class ManualTaskRunner : public ITaskRunner {
  public:
    void runInBackground(std::function<void()> task) override { background.push_back(std::move(task)); }
    void runOnMainThread(std::function<void()> task) override { main.push_back(std::move(task)); }

    static void drain(std::deque<std::function<void()>>& queue) {
        while (!queue.empty()) {
            auto task = std::move(queue.front());
            queue.pop_front();
            task();
        }
    }

    std::deque<std::function<void()>> background;
    std::deque<std::function<void()>> main;
};

GameEntry game(std::string title, std::string system) {
    GameEntry entry;
    entry.title = std::move(title);
    entry.system = std::move(system);
    return entry;
}

}  // namespace

// --- MockRemoteSource ----------------------------------------------------

TEST(MockRemoteSource, DemoIndexIsCleanAndComplete) {
    MockRemoteSource source;
    ImmediateTaskRunner tasks;
    ShopService shop(source, tasks);

    auto index = shop.loadIndex();
    ASSERT_TRUE(index.ok()) << index.error().describe();
    EXPECT_TRUE(index.value().warnings.empty());
    EXPECT_GE(index.value().games.size(), 10u);
    EXPECT_FALSE(index.value().name.empty());

    bool foundPlatine = false;
    for (const GameEntry& entry : index.value().games) {
        EXPECT_NE(entry.system, "unknown") << entry.title;
        EXPECT_GT(entry.sizeBytes, 0u) << entry.title;
        EXPECT_EQ(entry.romUrl.rfind("ftp://mock.local/shop/roms/", 0), 0u) << entry.romUrl;
        if (entry.title == "Pokémon Platine") {
            foundPlatine = true;
            EXPECT_EQ(entry.fileName, "Pokemon Platine (France).nds");
            EXPECT_EQ(entry.boxartUrl, "ftp://mock.local/shop/boxart/nds/pokemon-platine.png");
        }
    }
    EXPECT_TRUE(foundPlatine);
}

TEST(MockRemoteSource, ServesCustomDocumentsAndFailures) {
    MockRemoteSource source(R"({"games": []})", "");
    EXPECT_EQ(source.fetchIndex().value(), R"({"games": []})");

    source.setFailure(makeError(ErrorCode::NetworkError, "NAS unreachable"));
    auto failed = source.fetchIndex();
    ASSERT_FALSE(failed.ok());
    EXPECT_EQ(failed.error().code, ErrorCode::NetworkError);

    source.setFailure(std::nullopt);
    EXPECT_TRUE(source.fetchIndex().ok());
    EXPECT_EQ(source.fetchCount(), 3);
}

TEST(MockRemoteSource, DescribeNeverLeaksCredentials) {
    MockRemoteSource source("{}", "ftp://mock.local/shop/index.json");
    EXPECT_EQ(source.describe(), "mock: ftp://mock.local/shop/index.json");
}

// --- ShopService ---------------------------------------------------------

TEST(ShopService, PropagatesTransportErrors) {
    MockRemoteSource source;
    source.setFailure(makeError(ErrorCode::AuthenticationFailed, "bad password"));
    ImmediateTaskRunner tasks;
    ShopService shop(source, tasks);

    auto index = shop.loadIndex();
    ASSERT_FALSE(index.ok());
    EXPECT_EQ(index.error().code, ErrorCode::AuthenticationFailed);
}

TEST(ShopService, PropagatesParseErrors) {
    MockRemoteSource source("{not json", "");
    ImmediateTaskRunner tasks;
    ShopService shop(source, tasks);

    auto index = shop.loadIndex();
    ASSERT_FALSE(index.ok());
    EXPECT_EQ(index.error().code, ErrorCode::ParseError);
}

TEST(ShopService, ResolvesEntriesAgainstTheSourceUrl) {
    MockRemoteSource source(R"({"games": [{"url": "a.sfc"}]})", "ftp://nas/shop/index.json");
    ImmediateTaskRunner tasks;
    ShopService shop(source, tasks);

    auto index = shop.loadIndex();
    ASSERT_TRUE(index.ok());
    ASSERT_EQ(index.value().games.size(), 1u);
    EXPECT_EQ(index.value().games[0].romUrl, "ftp://nas/shop/a.sfc");
}

TEST(ShopService, AsyncFetchesInBackgroundAndAnswersOnMainThread) {
    MockRemoteSource source;
    ManualTaskRunner tasks;
    ShopService shop(source, tasks);

    std::optional<std::size_t> received;
    shop.loadIndexAsync([&](Result<ShopListing> listing) {
        ASSERT_TRUE(listing.ok());
        received = listing.value().index.games.size();
    });

    EXPECT_EQ(source.fetchCount(), 0);  // nothing ran on the caller's thread
    ASSERT_EQ(tasks.background.size(), 1u);

    ManualTaskRunner::drain(tasks.background);
    EXPECT_EQ(source.fetchCount(), 1);
    EXPECT_FALSE(received.has_value());  // not delivered until the main thread runs
    ASSERT_EQ(tasks.main.size(), 1u);

    ManualTaskRunner::drain(tasks.main);
    ASSERT_TRUE(received.has_value());
    EXPECT_GE(*received, 10u);
}

TEST(ShopService, AsyncDeliversErrors) {
    MockRemoteSource source;
    source.setFailure(makeError(ErrorCode::NetworkError, "timeout"));
    ImmediateTaskRunner tasks;
    ShopService shop(source, tasks);

    std::optional<ErrorCode> code;
    shop.loadIndexAsync([&](Result<ShopListing> listing) { code = listing.ok() ? ErrorCode::IoError : listing.error().code; });
    EXPECT_EQ(code, ErrorCode::NetworkError);
}

TEST(ShopService, GroupsBySystemSortedByDisplayName) {
    std::vector<SystemSection> sections = ShopService::groupBySystem({
        game("zelda", "snes"),
        game("Advance Wars", "gba"),
        game("Mystery", "unknown"),
        game("Aladdin", "snes"),
        game("Tetris", "gb"),
        game("Other", "zzz-custom"),
    });

    ASSERT_EQ(sections.size(), 5u);
    // Known systems by display name: Game Boy, Game Boy Advance, Super Nintendo...
    EXPECT_EQ(sections[0].displayName, "Game Boy");
    EXPECT_EQ(sections[1].displayName, "Game Boy Advance");
    EXPECT_EQ(sections[2].displayName, "Super Nintendo");
    // ...then unknown ones.
    EXPECT_EQ(sections[3].system, "unknown");
    EXPECT_EQ(sections[4].system, "zzz-custom");

    ASSERT_EQ(sections[2].games.size(), 2u);
    EXPECT_EQ(sections[2].games[0].title, "Aladdin");  // case-insensitive order
    EXPECT_EQ(sections[2].games[1].title, "zelda");
}

TEST(ShopService, GroupingEmptyListGivesNoSection) {
    EXPECT_TRUE(ShopService::groupBySystem({}).empty());
}

TEST(ShopService, ReportsWhichGamesAreAlreadyOnTheCard) {
    test::MemoryFileSystem fs;
    ASSERT_TRUE(fs.createDirectories("/roms/nds").ok());
    ASSERT_TRUE(fs.writeFile("/roms/nds/Pokemon Platine (France).nds", "rom").ok());
    RomStore store(fs, SdLayout{});
    MockRemoteSource source;
    ImmediateTaskRunner tasks;
    ShopService shop(source, tasks, &store);

    std::optional<ShopListing> received;
    shop.loadIndexAsync([&](Result<ShopListing> listing) {
        ASSERT_TRUE(listing.ok());
        received = listing.value();
    });

    ASSERT_TRUE(received.has_value());
    EXPECT_EQ(received->installedIds, (std::set<std::string>{"nds/Pokemon Platine (France).nds"}));
}

TEST(ShopService, WithoutAStoreNothingIsInstalled) {
    MockRemoteSource source;
    ImmediateTaskRunner tasks;
    ShopService shop(source, tasks);
    auto listing = shop.loadListing();
    ASSERT_TRUE(listing.ok());
    EXPECT_TRUE(listing.value().installedIds.empty());
    EXPECT_GE(listing.value().index.games.size(), 10u);
}
