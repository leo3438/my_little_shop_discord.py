#include "retromanager/core/EventBus.hpp"

#include <algorithm>

namespace rm {

EventBus::Subscription::Subscription(EventBus* bus, std::type_index type, std::shared_ptr<void> slot,
                                     std::shared_ptr<std::atomic<bool>> alive)
    : bus_(bus), type_(type), slot_(std::move(slot)), alive_(std::move(alive)) {}

EventBus::Subscription::Subscription(Subscription&& other) noexcept
    : bus_(other.bus_), type_(other.type_), slot_(std::move(other.slot_)), alive_(std::move(other.alive_)) {
    other.bus_ = nullptr;
}

EventBus::Subscription& EventBus::Subscription::operator=(Subscription&& other) noexcept {
    if (this != &other) {
        reset();
        bus_ = other.bus_;
        type_ = other.type_;
        slot_ = std::move(other.slot_);
        alive_ = std::move(other.alive_);
        other.bus_ = nullptr;
    }
    return *this;
}

void EventBus::Subscription::reset() {
    if (bus_ == nullptr) return;
    alive_->store(false);  // events already queued will skip this handler
    bus_->unsubscribe(type_, slot_);
    bus_ = nullptr;
    slot_.reset();
    alive_.reset();
}

bool EventBus::Subscription::active() const { return bus_ != nullptr; }

void EventBus::unsubscribe(std::type_index type, const std::shared_ptr<void>& slot) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = slots_.find(type);
    if (it == slots_.end()) return;
    auto& list = it->second;
    list.erase(std::remove(list.begin(), list.end(), slot), list.end());
}

std::size_t EventBus::subscriberCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::size_t count = 0;
    for (const auto& entry : slots_) count += entry.second.size();
    return count;
}

}  // namespace rm
