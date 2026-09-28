#pragma once

#include <atomic>
#include <chrono>
#include <map>
#include <memory>
#include <optional>
#include <string>

#include "retromanager/network/IRemoteSource.hpp"

namespace rm {

// In-memory IRemoteSource.
//
// Default-constructed, it is the demo shop: a static index plus one
// synthetic file per entry, generated on the fly (a "128 MB" demo ROM never
// exists in memory). Used by the tests and by the app when config.json says
// "type": "mock".
//
// Latency, throughput and failures can be simulated to exercise loading,
// progress and error states. Configure it before sharing it with other
// threads.
class MockRemoteSource : public IRemoteSource {
  public:
    static constexpr const char* kDemoIndexUrl = "ftp://mock.local/shop/index.json";

    MockRemoteSource();  // demo shop
    MockRemoteSource(std::string document, std::string indexUrl);
    static std::unique_ptr<MockRemoteSource> createDemo() { return std::make_unique<MockRemoteSource>(); }

    // The demo index document.
    static std::string demoIndex();

    // Byte at `offset` of every synthetic file (lets tests verify content).
    static char syntheticByte(std::uint64_t offset);

    std::string describe() const override;
    std::string indexUrl() const override { return indexUrl_; }
    Result<std::string> fetchIndex() override;
    Status downloadFile(const std::string& url, const ChunkSink& sink, const ProgressCallback& progress,
                        const CancellationToken& cancel) override;

    void addFile(const std::string& url, std::string content);
    void addSyntheticFile(const std::string& url, std::uint64_t size);

    // Every subsequent call sleeps this long before answering.
    void setLatency(std::chrono::milliseconds latency) { latency_ = latency; }
    // Every subsequent call fails with `error` (nullopt restores success).
    void setFailure(std::optional<Error> error) { failure_ = std::move(error); }
    // Downloads fail with `error` once `afterBytes` bytes were delivered
    // (a dropped connection).
    void setDownloadFailure(std::uint64_t afterBytes, Error error) {
        dropAfter_ = afterBytes;
        dropError_ = std::move(error);
    }
    // Download speed cap in bytes per second (0 = unlimited).
    void setThroughput(std::uint64_t bytesPerSecond) { throughput_ = bytesPerSecond; }
    void setChunkSize(std::size_t bytes) { chunkSize_ = bytes; }

    int fetchCount() const { return fetchCount_.load(); }
    int downloadCount() const { return downloadCount_.load(); }

  private:
    struct File {
        std::string content;      // real content, or...
        std::uint64_t size = 0;   // ...synthetic size when `synthetic`
        bool synthetic = false;
    };

    std::string document_;
    std::string indexUrl_;
    std::map<std::string, File> files_;
    std::chrono::milliseconds latency_{0};
    std::optional<Error> failure_;
    std::optional<std::uint64_t> dropAfter_;
    Error dropError_{ErrorCode::NetworkError, ""};
    std::uint64_t throughput_ = 0;
    std::size_t chunkSize_ = 16 * 1024;  // like curl's default write chunk
    std::atomic<int> fetchCount_{0};
    std::atomic<int> downloadCount_{0};
};

}  // namespace rm
