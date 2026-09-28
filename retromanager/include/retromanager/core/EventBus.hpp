#pragma once

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <typeindex>
#include <vector>

#include "retromanager/core/ITaskRunner.hpp"

namespace rm {

// Typed publish/subscribe channel between services and the UI.
//
// - publish() may be called from any thread.
// - Handlers always run on the main thread (through ITaskRunner), so UI
//   code can touch views from a handler directly.
// - Subscriptions are RAII: destroying the Subscription (on the main
//   thread) guarantees the handler will never be called again, even for
//   events already queued.
//
// Events are plain structs (see services/DownloadEvents.hpp).
class EventBus {
  public:
    class Subscription {
      public:
        Subscription() = default;
        Subscription(Subscription&& other) noexcept;
        Subscription& operator=(Subscription&& other) noexcept;
        Subscription(const Subscription&) = delete;
        Subscription& operator=(const Subscription&) = delete;
        ~Subscription() { reset(); }

        void reset();
        bool active() const;

      private:
        friend class EventBus;
        Subscription(EventBus* bus, std::type_index type, std::shared_ptr<void> slot, std::shared_ptr<std::atomic<bool>> alive);

        EventBus* bus_ = nullptr;
        std::type_index type_ = typeid(void);
        std::shared_ptr<void> slot_;
        std::shared_ptr<std::atomic<bool>> alive_;
    };

    explicit EventBus(ITaskRunner& dispatcher) : dispatcher_(dispatcher) {}
    EventBus(const EventBus&) = delete;
    EventBus& operator=(const EventBus&) = delete;

    // The bus must outlive its subscriptions.
    template <typename Event>
    [[nodiscard]] Subscription subscribe(std::function<void(const Event&)> handler) {
        auto alive = std::make_shared<std::atomic<bool>>(true);
        auto slot = std::make_shared<Slot<Event>>(Slot<Event>{std::move(handler), alive});
        {
            std::lock_guard<std::mutex> lock(mutex_);
            slots_[typeid(Event)].push_back(slot);
        }
        return Subscription(this, typeid(Event), slot, alive);
    }

    template <typename Event>
    void publish(Event event) {
        std::vector<std::shared_ptr<void>> targets;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = slots_.find(typeid(Event));
            if (it == slots_.end() || it->second.empty()) return;
            targets = it->second;
        }
        auto shared = std::make_shared<const Event>(std::move(event));
        dispatcher_.runOnMainThread([targets = std::move(targets), shared]() {
            for (const auto& target : targets) {
                auto* slot = static_cast<Slot<Event>*>(target.get());
                if (slot->alive->load()) slot->handler(*shared);  // re-checked at delivery time
            }
        });
    }

    std::size_t subscriberCount() const;

  private:
    template <typename Event>
    struct Slot {
        std::function<void(const Event&)> handler;
        std::shared_ptr<std::atomic<bool>> alive;
    };

    void unsubscribe(std::type_index type, const std::shared_ptr<void>& slot);

    ITaskRunner& dispatcher_;
    mutable std::mutex mutex_;
    std::map<std::type_index, std::vector<std::shared_ptr<void>>> slots_;
};

}  // namespace rm
