#include <gtest/gtest.h>

#include <atomic>
#include <deque>
#include <string>
#include <thread>
#include <vector>

#include "retromanager/core/Cancellation.hpp"
#include "retromanager/core/Crc32.hpp"
#include "retromanager/core/EventBus.hpp"
#include "retromanager/core/Url.hpp"
#include "retromanager/core/WorkerThread.hpp"
#include "retromanager/platform/BufferedWriteStream.hpp"

using namespace rm;

namespace {

class QueueRunner : public ITaskRunner {
  public:
    void runInBackground(std::function<void()> task) override { task(); }
    void runOnMainThread(std::function<void()> task) override { main.push_back(std::move(task)); }
    void drain() {
        while (!main.empty()) {
            auto task = std::move(main.front());
            main.pop_front();
            task();
        }
    }
    std::deque<std::function<void()>> main;
};

// Records every write reaching the "disk".
class RecordingStream : public IWriteStream {
  public:
    struct Log {
        std::vector<std::size_t> writes;
        std::string data;
        bool closed = false;
        bool destroyed = false;
    };
    explicit RecordingStream(Log& log, int failAtWrite = -1) : log_(log), failAt_(failAtWrite) {}
    ~RecordingStream() override { log_.destroyed = true; }

    Status write(const char* data, std::size_t size) override {
        if (static_cast<int>(log_.writes.size()) == failAt_) return makeError(ErrorCode::IoError, "disk full");
        log_.writes.push_back(size);
        log_.data.append(data, size);
        return success();
    }
    Status close() override {
        log_.closed = true;
        return success();
    }

  private:
    Log& log_;
    int failAt_;
};

struct Ping {
    int value;
};
struct Pong {
    std::string text;
};

}  // namespace

// --- Crc32 ---------------------------------------------------------------

TEST(Crc32, MatchesReferenceVectors) {
    EXPECT_EQ(Crc32::of(""), 0x00000000u);
    EXPECT_EQ(Crc32::of("123456789"), 0xCBF43926u);  // the standard check value
    EXPECT_EQ(Crc32::of("The quick brown fox jumps over the lazy dog"), 0x414FA339u);
}

TEST(Crc32, IncrementalEqualsOneShot) {
    std::string data(100000, '\0');
    for (std::size_t i = 0; i < data.size(); ++i) data[i] = static_cast<char>(i * 31 + 7);

    Crc32 crc;
    for (std::size_t offset = 0; offset < data.size(); offset += 4093) {
        crc.update(data.data() + offset, std::min<std::size_t>(4093, data.size() - offset));
    }
    EXPECT_EQ(crc.value(), Crc32::of(data));
}

TEST(Crc32, HexIsEightLowercaseDigits) {
    Crc32 crc;
    crc.update("123456789", 9);
    EXPECT_EQ(crc.hex(), "cbf43926");

    Crc32 small;
    small.update("\x00", 1);  // 0xd202ef8d
    EXPECT_EQ(small.hex().size(), 8u);
}

// --- CancellationToken ---------------------------------------------------

TEST(CancellationToken, StartsActiveAndLatches) {
    CancellationToken token;
    EXPECT_FALSE(token.isCancelled());
    token.cancel();
    EXPECT_TRUE(token.isCancelled());
    token.cancel();
    EXPECT_TRUE(token.isCancelled());
}

// --- url::split ----------------------------------------------------------

TEST(UrlSplit, FullUrl) {
    auto parts = url::split("FTP://leo:pw@NAS.local:2121/shop/index.json?x=1#frag");
    ASSERT_TRUE(parts.ok()) << parts.error().describe();
    EXPECT_EQ(parts.value().scheme, "ftp");
    EXPECT_EQ(parts.value().userInfo, "leo:pw");
    EXPECT_EQ(parts.value().host, "nas.local");
    EXPECT_EQ(parts.value().port, 2121);
    EXPECT_EQ(parts.value().path, "/shop/index.json");
    EXPECT_EQ(parts.value().query, "x=1");
    EXPECT_EQ(parts.value().fragment, "frag");
}

TEST(UrlSplit, MinimalAndIpv6) {
    auto bare = url::split("ftp://192.168.1.20");
    ASSERT_TRUE(bare.ok());
    EXPECT_EQ(bare.value().host, "192.168.1.20");
    EXPECT_FALSE(bare.value().port.has_value());
    EXPECT_EQ(bare.value().path, "/");

    auto v6 = url::split("ftps://[fe80::1]:990/a%20b");
    ASSERT_TRUE(v6.ok());
    EXPECT_EQ(v6.value().host, "fe80::1");
    EXPECT_EQ(v6.value().port, 990);
    EXPECT_EQ(v6.value().path, "/a%20b");
}

TEST(UrlSplit, RejectsInvalidUrls) {
    for (const char* bad : {"", "nas.local/index.json", "ftp:/nas/x", "ftp://", "ftp://:21/x", "ftp://nas:0/x",
                            "ftp://nas:99999/x", "ftp://nas:abc/x", "ftp://[fe80::1/x"}) {
        auto parts = url::split(bad);
        EXPECT_FALSE(parts.ok()) << bad;
        if (!parts.ok()) {
            EXPECT_EQ(parts.error().code, ErrorCode::InvalidArgument) << bad;
        }
    }
}

// --- EventBus ------------------------------------------------------------

TEST(EventBus, DeliversTypedEventsOnTheMainThread) {
    QueueRunner runner;
    EventBus bus(runner);
    std::vector<int> pings;
    std::vector<std::string> pongs;
    auto s1 = bus.subscribe<Ping>([&](const Ping& e) { pings.push_back(e.value); });
    auto s2 = bus.subscribe<Pong>([&](const Pong& e) { pongs.push_back(e.text); });

    bus.publish(Ping{1});
    bus.publish(Pong{"a"});
    bus.publish(Ping{2});
    EXPECT_TRUE(pings.empty());  // queued, not delivered on the publisher's thread

    runner.drain();
    EXPECT_EQ(pings, (std::vector<int>{1, 2}));
    EXPECT_EQ(pongs, (std::vector<std::string>{"a"}));
}

TEST(EventBus, FansOutToEverySubscriber) {
    ImmediateTaskRunner runner;
    EventBus bus(runner);
    int a = 0, b = 0;
    auto s1 = bus.subscribe<Ping>([&](const Ping& e) { a += e.value; });
    auto s2 = bus.subscribe<Ping>([&](const Ping& e) { b += e.value; });
    bus.publish(Ping{5});
    EXPECT_EQ(a, 5);
    EXPECT_EQ(b, 5);
    EXPECT_EQ(bus.subscriberCount(), 2u);
}

TEST(EventBus, DestroyedSubscriptionIsNeverCalledAgain) {
    QueueRunner runner;
    EventBus bus(runner);
    int calls = 0;
    {
        auto subscription = bus.subscribe<Ping>([&](const Ping&) { ++calls; });
        bus.publish(Ping{1});  // queued while subscribed...
    }                          // ...then unsubscribed before delivery
    runner.drain();
    EXPECT_EQ(calls, 0);
    EXPECT_EQ(bus.subscriberCount(), 0u);

    bus.publish(Ping{2});  // no subscriber: nothing queued at all
    EXPECT_TRUE(runner.main.empty());
}

TEST(EventBus, SubscriptionsAreMovableAndResettable) {
    ImmediateTaskRunner runner;
    EventBus bus(runner);
    int calls = 0;
    EventBus::Subscription kept;
    {
        auto temporary = bus.subscribe<Ping>([&](const Ping&) { ++calls; });
        kept = std::move(temporary);
    }
    bus.publish(Ping{1});
    EXPECT_EQ(calls, 1);
    EXPECT_TRUE(kept.active());

    kept.reset();
    EXPECT_FALSE(kept.active());
    bus.publish(Ping{1});
    EXPECT_EQ(calls, 1);
}

TEST(EventBus, PublishIsThreadSafe) {
    QueueRunner runner;
    std::mutex queueMutex;
    // Wrap the runner so that concurrent publishers enqueue safely.
    class LockedRunner : public ITaskRunner {
      public:
        LockedRunner(QueueRunner& inner, std::mutex& m) : inner_(inner), m_(m) {}
        void runInBackground(std::function<void()> t) override { t(); }
        void runOnMainThread(std::function<void()> t) override {
            std::lock_guard<std::mutex> lock(m_);
            inner_.runOnMainThread(std::move(t));
        }

      private:
        QueueRunner& inner_;
        std::mutex& m_;
    } locked(runner, queueMutex);

    EventBus bus(locked);
    int total = 0;
    auto subscription = bus.subscribe<Ping>([&](const Ping& e) { total += e.value; });

    std::vector<std::thread> publishers;
    for (int t = 0; t < 4; ++t) {
        publishers.emplace_back([&bus] {
            for (int i = 0; i < 250; ++i) bus.publish(Ping{1});
        });
    }
    for (auto& thread : publishers) thread.join();
    runner.drain();
    EXPECT_EQ(total, 1000);
}

// --- WorkerThread --------------------------------------------------------

TEST(WorkerThread, RunsTasksInOrderOffTheCallingThread) {
    std::vector<std::function<void()>> mainTasks;
    std::mutex mainMutex;
    WorkerThread worker([&](std::function<void()> task) {
        std::lock_guard<std::mutex> lock(mainMutex);
        mainTasks.push_back(std::move(task));
    });

    const auto caller = std::this_thread::get_id();
    std::vector<int> order;
    std::atomic<bool> offThread{true};
    for (int i = 0; i < 5; ++i) {
        worker.runInBackground([&, i] {
            if (std::this_thread::get_id() == caller) offThread = false;
            order.push_back(i);
            if (i == 4) worker.runOnMainThread([] {});
        });
    }
    worker.waitIdle();
    EXPECT_TRUE(offThread);
    EXPECT_EQ(order, (std::vector<int>{0, 1, 2, 3, 4}));
    std::lock_guard<std::mutex> lock(mainMutex);
    EXPECT_EQ(mainTasks.size(), 1u);  // forwarded to the main-thread dispatcher
}

TEST(WorkerThread, DestructorWaitsForTheRunningTask) {
    std::atomic<bool> finished{false};
    {
        WorkerThread worker([](std::function<void()>) {});
        worker.runInBackground([&] {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            finished = true;
        });
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    EXPECT_TRUE(finished);
}

// --- BufferedWriteStream -------------------------------------------------

TEST(BufferedWriteStream, CoalescesSmallWritesIntoCapacitySizedOnes) {
    RecordingStream::Log log;
    BufferedWriteStream stream(std::make_unique<RecordingStream>(log), 1000);

    std::string expected;
    for (int i = 0; i < 350; ++i) {  // 350 x 10 bytes = 3500 bytes
        std::string chunk(10, static_cast<char>('a' + i % 26));
        expected += chunk;
        ASSERT_TRUE(stream.write(chunk.data(), chunk.size()).ok());
    }
    ASSERT_TRUE(stream.close().ok());

    EXPECT_EQ(log.data, expected);
    EXPECT_EQ(log.writes, (std::vector<std::size_t>{1000, 1000, 1000, 500}));
    EXPECT_TRUE(log.closed);
}

TEST(BufferedWriteStream, NeverWritesMoreThanCapacityAtOnce) {
    RecordingStream::Log log;
    BufferedWriteStream stream(std::make_unique<RecordingStream>(log), 1024);
    std::string big(5000, 'x');  // one write larger than the buffer
    ASSERT_TRUE(stream.write(big.data(), big.size()).ok());
    ASSERT_TRUE(stream.close().ok());

    std::size_t total = 0;
    for (std::size_t size : log.writes) {
        EXPECT_LE(size, 1024u);
        total += size;
    }
    EXPECT_EQ(total, 5000u);
}

TEST(BufferedWriteStream, PropagatesInnerErrors) {
    RecordingStream::Log log;
    BufferedWriteStream stream(std::make_unique<RecordingStream>(log, /*failAtWrite=*/1), 100);
    std::string chunk(100, 'x');
    EXPECT_TRUE(stream.write(chunk.data(), 100).ok());  // buffer full: flush #0 succeeds
    Status failed = stream.write(chunk.data(), 100);    // flush #1 fails
    ASSERT_FALSE(failed.ok());
    EXPECT_EQ(failed.error().code, ErrorCode::IoError);
}

TEST(BufferedWriteStream, AbandonedStreamNeverCommits) {
    RecordingStream::Log log;
    {
        BufferedWriteStream stream(std::make_unique<RecordingStream>(log), 100);
        ASSERT_TRUE(stream.write("abc", 3).ok());
    }
    EXPECT_FALSE(log.closed);
    EXPECT_TRUE(log.destroyed);  // inner stream discarded -> staging file removed
}

TEST(BufferedWriteStream, EmptyFileStillCommits) {
    RecordingStream::Log log;
    BufferedWriteStream stream(std::make_unique<RecordingStream>(log), 100);
    ASSERT_TRUE(stream.close().ok());
    EXPECT_TRUE(log.closed);
    EXPECT_TRUE(log.writes.empty());
}
