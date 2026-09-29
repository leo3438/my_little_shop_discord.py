// The download queue survives the app: /switch/RetroManager/queue.json is
// rewritten on every change, and reloaded at startup, where interrupted
// downloads resume from their .tmp file.

#include <gtest/gtest.h>

#include <chrono>
#include <deque>
#include <mutex>
#include <thread>

#include "MemoryFileSystem.hpp"
#include "MockSdCard.hpp"
#include "retromanager/core/WorkerThread.hpp"
#include "retromanager/network/MockRemoteSource.hpp"
#include "retromanager/parsers/EntryJson.hpp"
#include "retromanager/services/AppManager.hpp"
#include "retromanager/services/DownloadQueueManager.hpp"
#include "retromanager/services/QueueStore.hpp"

using namespace rm;

namespace {

GameEntry fullGame() {
    GameEntry g;
    g.id = "nds/Pokémon Platine (France).nds";
    g.title = "Pokémon Platine";
    g.system = "nds";
    g.region = "EUR";
    g.sizeBytes = 134217728;
    g.romUrl = "ftp://nas.local/shop/roms/nds/Pok%C3%A9mon%20Platine.nds";
    g.fileName = "Pokémon Platine (France).nds";
    g.boxartUrl = "https://example.org/a.png";
    g.crc32 = "9a2d6a7e";
    g.year = 2009;
    g.description = "Version française.";
    g.cheatUrl = "ftp://nas.local/shop/cheats/a.cht";
    return g;
}

AppEntry fullApp() {
    AppEntry a;
    a.id = "app/RetroArch";
    a.title = "RetroArch";
    a.author = "libretro";
    a.version = "1.19.1";
    a.description = "Frontend.";
    a.nroUrl = "https://example.org/retroarch_switch.nro";
    a.iconUrl = "https://example.org/icon.jpg";
    a.folder = "RetroArch";
    a.sizeBytes = 25165824;
    a.crc32 = "0a1b2c3d";
    a.category = AppCategory::Emulator;
    return a;
}

std::string urlFor(const std::string& name) { return "ftp://mock.local/shop/roms/nds/" + name + ".nds"; }

GameEntry game(const std::string& name, std::uint64_t size = 4096) {
    GameEntry entry;
    entry.id = "nds/" + name + ".nds";
    entry.title = name;
    entry.system = "nds";
    entry.fileName = name + ".nds";
    entry.sizeBytes = size;
    entry.romUrl = urlFor(name);
    return entry;
}

class ManualRunner : public ITaskRunner {
  public:
    void runInBackground(std::function<void()> task) override { tasks.push_back(std::move(task)); }
    void runOnMainThread(std::function<void()> task) override { task(); }
    void runAll() {
        while (!tasks.empty()) {
            auto task = std::move(tasks.front());
            tasks.pop_front();
            task();
        }
    }
    std::deque<std::function<void()>> tasks;
};

std::vector<std::string> titles(const std::vector<QueueItem>& items) {
    std::vector<std::string> out;
    for (const QueueItem& item : items) out.push_back(item.title);
    return out;
}

}  // namespace

// --- entries as JSON -----------------------------------------------------------

TEST(EntryJson, GamesAndAppsRoundTrip) {
    auto game = gameFromJson(gameToJson(fullGame()));
    ASSERT_TRUE(game.ok()) << game.error().describe();
    EXPECT_EQ(game.value(), fullGame());
    GameEntry noYear = fullGame();
    noYear.year.reset();
    EXPECT_EQ(gameFromJson(gameToJson(noYear)).value(), noYear);

    auto app = appFromJson(appToJson(fullApp()));
    ASSERT_TRUE(app.ok()) << app.error().describe();
    EXPECT_EQ(app.value(), fullApp());
}

TEST(EntryJson, RejectsGarbage) {
    EXPECT_FALSE(gameFromJson("").ok());
    EXPECT_FALSE(gameFromJson("[]").ok());
    EXPECT_FALSE(gameFromJson(R"({"title": "x"})").ok());  // no URL
    EXPECT_FALSE(appFromJson(R"({"title": "x", "nro_url": "u"})").ok());  // no folder
}

// --- queue.json ------------------------------------------------------------------

TEST(QueueStore, SavesAndLoadsThePendingItems) {
    test::MemoryFileSystem fs;
    QueueStore store(fs, "/switch/RetroManager/queue.json");
    EXPECT_TRUE(store.load().empty());  // no file yet

    std::vector<std::string> payloads = {queuePayload(fullGame()), queuePayload(fullApp())};
    ASSERT_TRUE(store.save(payloads).ok());
    EXPECT_EQ(store.load(), payloads);
    EXPECT_NE(fs.readFile("/switch/RetroManager/queue.json").value().find("\"Pokémon Platine\""), std::string::npos);

    ASSERT_TRUE(fs.writeFile("/switch/RetroManager/queue.json", "{ broken").ok());
    EXPECT_TRUE(store.load().empty());  // a damaged file never blocks the app
}

TEST(QueuePersistence, EveryChangeIsSaved) {
    test::MemoryFileSystem fs;
    MockRemoteSource source("{}", "");
    for (const char* name : {"A", "B", "C"}) source.addSyntheticFile(urlFor(name), 4096);
    RomStore store(fs, SdLayout{});
    ImmediateTaskRunner main;
    EventBus bus(main);
    NullSystem system;
    auto* worker = new ManualRunner();
    std::vector<std::vector<std::string>> saves;
    {
        DownloadQueueManager queue(source, store, bus, system, std::unique_ptr<ITaskRunner>(worker));
        queue.setPersistence([&](const std::vector<std::string>& payloads) { saves.push_back(payloads); });

        queue.start(game("A"));
        DownloadId b = queue.start(game("B"));
        ASSERT_FALSE(saves.empty());
        EXPECT_EQ(saves.back(), (std::vector<std::string>{queuePayload(game("A")), queuePayload(game("B"))}));
        queue.cancel(b);
        EXPECT_EQ(saves.back(), (std::vector<std::string>{queuePayload(game("A"))}));
        worker->runAll();
        EXPECT_TRUE(saves.back().empty());  // done: nothing left to resume

        queue.start(game("C"));
        ASSERT_EQ(saves.back().size(), 1u);
    }  // quitting with C still pending: the last save must keep it
    EXPECT_EQ(saves.back(), (std::vector<std::string>{queuePayload(game("C"))}));
}

TEST(QueuePersistence, RestoresGamesAndAppsAndSkipsGarbage) {
    auto sd = test::makeMockSdCard();
    MockRemoteSource source("{}", "");
    RomStore store(*sd, SdLayout{});
    ImmediateTaskRunner main;
    EventBus bus(main);
    NullSystem system;
    auto* worker = new ManualRunner();
    DownloadQueueManager queue(source, store, bus, system, std::unique_ptr<ITaskRunner>(worker));
    AppManager apps(*sd, SdLayout{}, source);

    std::size_t restored = restoreQueue({queuePayload(game("A")), "{not json", queuePayload(fullApp()),
                                         R"({"kind": "tape", "entry": {}})"},
                                        queue, apps);
    EXPECT_EQ(restored, 2u);
    auto items = queue.snapshot();
    EXPECT_EQ(titles(items), (std::vector<std::string>{"A", "RetroArch"}));
    EXPECT_EQ(items[1].kind, DownloadKind::App);
}

TEST(QueuePersistence, AQueueInterruptedByQuittingResumesAtTheNextLaunch) {
    auto sd = test::makeMockSdCard();
    MockRemoteSource source("{}", "");
    source.addSyntheticFile(urlFor("Big"), 8 * 1024 * 1024);
    source.addSyntheticFile(urlFor("Small"), 4096);
    source.setThroughput(16 * 1024 * 1024);  // ~0.5 s
    RomStore store(*sd, SdLayout{});
    QueueStore file(*sd, "/switch/RetroManager/queue.json");
    std::mutex mutex;
    std::deque<std::function<void()>> mainQueue;
    class QueueRunner : public ITaskRunner {
      public:
        QueueRunner(std::mutex& m, std::deque<std::function<void()>>& q) : m_(m), q_(q) {}
        void runInBackground(std::function<void()> t) override { t(); }
        void runOnMainThread(std::function<void()> t) override {
            std::lock_guard<std::mutex> lock(m_);
            q_.push_back(std::move(t));
        }

      private:
        std::mutex& m_;
        std::deque<std::function<void()>>& q_;
    } main(mutex, mainQueue);
    EventBus bus(main);
    NullSystem system;
    auto newWorker = [&] { return std::make_unique<WorkerThread>([&](std::function<void()> t) { main.runOnMainThread(t); }); };

    {  // first launch: quit while "Big" downloads
        DownloadQueueManager queue(source, store, bus, system, newWorker());
        queue.setPersistence([&](const std::vector<std::string>& p) { file.save(p).ok(); });
        queue.start(game("Big", 8 * 1024 * 1024));
        queue.start(game("Small"));
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
    }
    EXPECT_EQ(file.load().size(), 2u);  // both still to do
    auto partial = sd->stat(stagingPath("/roms/nds/Big.nds"));
    ASSERT_TRUE(partial.ok());
    EXPECT_GT(partial.value().size, 0u);

    {  // second launch
        DownloadQueueManager queue(source, store, bus, system, newWorker());
        AppManager apps(*sd, SdLayout{}, source);
        queue.setPersistence([&](const std::vector<std::string>& p) { file.save(p).ok(); });
        EXPECT_EQ(restoreQueue(file.load(), queue, apps), 2u);
        for (int i = 0; i < 300 && queue.activeCount() > 0; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
        EXPECT_EQ(queue.activeCount(), 0u);
    }
    EXPECT_GT(source.maxOffset(), 0u);  // "Big" resumed from its partial file (REST / Range)
    EXPECT_TRUE(sd->isFile("/roms/nds/Big.nds"));
    EXPECT_EQ(sd->stat("/roms/nds/Big.nds").value().size, 8u * 1024 * 1024);
    EXPECT_TRUE(sd->isFile("/roms/nds/Small.nds"));
    EXPECT_TRUE(file.load().empty());
}
