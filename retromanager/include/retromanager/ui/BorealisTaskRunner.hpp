#pragma once

#include <borealis.hpp>

#include "retromanager/core/ITaskRunner.hpp"

namespace rm::ui {

// ITaskRunner on top of Borealis' thread pool. Services stay unaware of
// Borealis; only the UI layer provides this implementation.
class BorealisTaskRunner : public ITaskRunner {
  public:
    void runInBackground(std::function<void()> task) override { brls::async(task); }
    void runOnMainThread(std::function<void()> task) override { brls::sync(task); }
};

}  // namespace rm::ui
