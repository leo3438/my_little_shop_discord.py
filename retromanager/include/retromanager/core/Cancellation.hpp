#pragma once

#include <atomic>

namespace rm {

// Cooperative cancellation flag, shared between the thread that requests a
// stop (the UI) and the one doing the work (a download), which polls it.
class CancellationToken {
  public:
    void cancel() { cancelled_.store(true, std::memory_order_relaxed); }
    bool isCancelled() const { return cancelled_.load(std::memory_order_relaxed); }

  private:
    std::atomic<bool> cancelled_{false};
};

}  // namespace rm
