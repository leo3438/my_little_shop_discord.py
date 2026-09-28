#pragma once

#include <borealis.hpp>

#include "retromanager/core/EventBus.hpp"
#include "retromanager/models/GameEntry.hpp"
#include "retromanager/services/DownloadService.hpp"

namespace rm::ui {

// Modal download screen: starts the download, shows its progress from the
// EventBus, B cancels (the service deletes the staging file), then A/B
// closes once finished.
class DownloadActivity : public brls::Activity {
  public:
    DownloadActivity(DownloadService& downloads, EventBus& bus, GameEntry game);
    ~DownloadActivity() override;

    CONTENT_FROM_XML_RES("activity/download.xml");

    void onContentAvailable() override;

  private:
    enum class State { Running, Cancelling, Configuring, Finished };

    void onStarted(const DownloadStarted& event);
    void onProgress(const DownloadProgressed& event);
    void onConfiguring(const DownloadConfiguring& event);
    void onFinished(const DownloadFinished& event);
    bool onBack();
    void close();
    void setProgress(double ratio);

    DownloadService& downloads_;
    EventBus& bus_;
    GameEntry game_;
    DownloadId id_ = 0;
    State state_ = State::Running;
    bool closing_ = false;
    EventBus::Subscription started_, progressed_, configuring_, finished_;

    BRLS_BIND(brls::Box, root, "download/root");
    BRLS_BIND(brls::Label, titleLabel, "download/title");
    BRLS_BIND(brls::Label, destinationLabel, "download/destination");
    BRLS_BIND(brls::Box, track, "download/track");
    BRLS_BIND(brls::Rectangle, fill, "download/fill");
    BRLS_BIND(brls::Label, statsLabel, "download/stats");
    BRLS_BIND(brls::Label, statusLabel, "download/status");
    BRLS_BIND(brls::Label, stepsLabel, "download/steps");
    BRLS_BIND(brls::Button, actionButton, "download/action");
};

}  // namespace rm::ui
