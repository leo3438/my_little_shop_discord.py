#pragma once

#include <functional>

namespace rm {

// Where work runs. Services post blocking work (network, disk) to the
// background and deliver results on the main thread, without knowing about
// Borealis.
//
// - App: ui::BorealisTaskRunner (brls::async / brls::sync).
// - Tests: ImmediateTaskRunner, which runs everything inline so tests are
//   synchronous and deterministic.
class ITaskRunner {
  public:
    virtual ~ITaskRunner() = default;
    virtual void runInBackground(std::function<void()> task) = 0;
    virtual void runOnMainThread(std::function<void()> task) = 0;
};

class ImmediateTaskRunner : public ITaskRunner {
  public:
    void runInBackground(std::function<void()> task) override { task(); }
    void runOnMainThread(std::function<void()> task) override { task(); }
};

}  // namespace rm
