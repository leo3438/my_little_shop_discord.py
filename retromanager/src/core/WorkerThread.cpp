#include "retromanager/core/WorkerThread.hpp"

namespace rm {

WorkerThread::WorkerThread(std::function<void(std::function<void()>)> mainThread)
    : mainThread_(std::move(mainThread)), thread_([this] { loop(); }) {}

WorkerThread::~WorkerThread() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
        queue_.clear();
    }
    wake_.notify_all();
    if (thread_.joinable()) thread_.join();
}

void WorkerThread::runInBackground(std::function<void()> task) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_) return;
        queue_.push_back(std::move(task));
    }
    wake_.notify_one();
}

void WorkerThread::waitIdle() {
    std::unique_lock<std::mutex> lock(mutex_);
    idle_.wait(lock, [this] { return queue_.empty() && !running_; });
}

void WorkerThread::loop() {
    std::unique_lock<std::mutex> lock(mutex_);
    while (true) {
        wake_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
        if (stopping_) break;
        std::function<void()> task = std::move(queue_.front());
        queue_.pop_front();
        running_ = true;
        lock.unlock();
        task();
        lock.lock();
        running_ = false;
        if (queue_.empty()) idle_.notify_all();
    }
    running_ = false;
    idle_.notify_all();
}

}  // namespace rm
