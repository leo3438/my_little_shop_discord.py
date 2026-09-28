#pragma once

#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>

#include "retromanager/core/ITaskRunner.hpp"

namespace rm {

// ITaskRunner with one dedicated background thread, for long jobs
// (downloads) that must not starve the shared UI task loop.
//
// Background tasks run one at a time, in submission order. Main-thread
// work is forwarded to `mainThread` (brls::sync in the app).
// The destructor drops tasks that have not started and joins the thread
// once the running one returns: cancel long work before destroying it.
class WorkerThread : public ITaskRunner {
  public:
    explicit WorkerThread(std::function<void(std::function<void()>)> mainThread);
    ~WorkerThread() override;

    WorkerThread(const WorkerThread&) = delete;
    WorkerThread& operator=(const WorkerThread&) = delete;

    void runInBackground(std::function<void()> task) override;
    void runOnMainThread(std::function<void()> task) override { mainThread_(std::move(task)); }

    // Blocks until every submitted task has run (tests, shutdown).
    void waitIdle();

  private:
    void loop();

    std::function<void(std::function<void()>)> mainThread_;
    std::mutex mutex_;
    std::condition_variable wake_;
    std::condition_variable idle_;
    std::deque<std::function<void()>> queue_;
    bool running_ = false;  // a task is executing
    bool stopping_ = false;
    std::thread thread_;
};

}  // namespace rm
