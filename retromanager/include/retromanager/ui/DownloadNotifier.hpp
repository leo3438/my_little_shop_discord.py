#pragma once

#include "retromanager/core/EventBus.hpp"
#include "retromanager/services/DownloadQueueManager.hpp"

namespace rm::ui {

// While the queue works in the background, whatever the screen: a
// notification when an item is installed or fails (cancellations are the
// user's own doing and stay silent).
class DownloadNotifier {
  public:
    explicit DownloadNotifier(EventBus& bus);

  private:
    EventBus::Subscription finished_;
};

}  // namespace rm::ui
