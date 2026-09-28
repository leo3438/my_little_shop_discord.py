#pragma once

#include <atomic>
#include <chrono>
#include <optional>
#include <string>

#include "retromanager/network/IRemoteSource.hpp"

namespace rm {

// In-memory IRemoteSource: serves a static index document.
//
// Used by the unit tests and, until a real source is configured, by the
// desktop and console app to exercise the whole shop flow without a NAS.
// Latency and failures can be simulated to exercise the UI's loading and
// error states. Configure it before sharing it with other threads.
class MockRemoteSource : public IRemoteSource {
  public:
    // A demo shop of well-formed entries across several systems.
    static const char* demoIndex();

    explicit MockRemoteSource(std::string document = demoIndex(),
                              std::string indexUrl = "ftp://mock.local/shop/index.json");

    std::string describe() const override;
    std::string indexUrl() const override { return indexUrl_; }
    Result<std::string> fetchIndex() override;

    // Every subsequent fetch sleeps this long before answering.
    void setLatency(std::chrono::milliseconds latency) { latency_ = latency; }
    // Every subsequent fetch fails with `error` (nullopt restores success).
    void setFailure(std::optional<Error> error) { failure_ = std::move(error); }

    int fetchCount() const { return fetchCount_.load(); }

  private:
    std::string document_;
    std::string indexUrl_;
    std::chrono::milliseconds latency_{0};
    std::optional<Error> failure_;
    std::atomic<int> fetchCount_{0};
};

}  // namespace rm
