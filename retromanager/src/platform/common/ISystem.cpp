#include "retromanager/platform/ISystem.hpp"

namespace rm {

AwakeLock::Holder::Holder(ISystem& system) : system_(system) {}

AwakeLock::Holder::~Holder() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (count_ > 0) system_.setKeepAwake(false);  // never leave the console unable to sleep
}

std::unique_ptr<AwakeLock> AwakeLock::Holder::acquire() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (count_++ == 0) system_.setKeepAwake(true);
    return std::unique_ptr<AwakeLock>(new AwakeLock(*this));
}

void AwakeLock::Holder::release() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (--count_ == 0) system_.setKeepAwake(false);
}

int AwakeLock::Holder::activeLocks() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return count_;
}

}  // namespace rm
