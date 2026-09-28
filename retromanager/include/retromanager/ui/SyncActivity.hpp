#pragma once

#include <borealis.hpp>

#include "retromanager/core/EventBus.hpp"
#include "retromanager/services/CloudSyncService.hpp"

namespace rm::ui {

// Modal cloud saves screen: starts a sync, shows which file is being
// handled and the overall progress, B cancels (files already exchanged
// stay exchanged), then a summary: "X sent, Y received" plus conflicts and
// failures, if any.
class SyncActivity : public brls::Activity {
  public:
    SyncActivity(CloudSyncService& sync, EventBus& bus);
    ~SyncActivity() override;

    CONTENT_FROM_XML_RES("activity/sync.xml");

    void onContentAvailable() override;

  private:
    enum class State { Running, Cancelling, Finished };

    void onProgress(const SyncProgressed& event);
    void onFinished(const SyncFinished& event);
    void finish(const std::string& status, const std::string& details);
    bool onBack();
    void close();
    void setProgress(double ratio);

    CloudSyncService& sync_;
    EventBus& bus_;
    State state_ = State::Running;
    bool closing_ = false;
    EventBus::Subscription progressed_, finished_;

    BRLS_BIND(brls::Box, root, "sync/root");
    BRLS_BIND(brls::Label, remoteLabel, "sync/remote");
    BRLS_BIND(brls::Box, track, "sync/track");
    BRLS_BIND(brls::Rectangle, fill, "sync/fill");
    BRLS_BIND(brls::Label, statsLabel, "sync/stats");
    BRLS_BIND(brls::Label, statusLabel, "sync/status");
    BRLS_BIND(brls::Label, detailsLabel, "sync/details");
    BRLS_BIND(brls::Button, actionButton, "sync/action");
};

}  // namespace rm::ui
