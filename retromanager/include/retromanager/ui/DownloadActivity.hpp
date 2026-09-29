#pragma once

#include <borealis.hpp>

#include <functional>

#include "retromanager/core/EventBus.hpp"
#include "retromanager/models/AppEntry.hpp"
#include "retromanager/models/GameEntry.hpp"
#include "retromanager/services/AppManager.hpp"
#include "retromanager/services/DownloadService.hpp"

namespace rm::ui {

// What the download screen shows, and how it starts the transfer.
struct DownloadRequest {
    enum class Kind { Rom, App };
    Kind kind = Kind::Rom;
    std::string title;
    std::uint64_t sizeBytes = 0;
    std::string system;                 // ROMs: names the playlist in the summary
    std::function<DownloadId()> start;  // called once, after the screen subscribed to the events

    static DownloadRequest forGame(DownloadService& downloads, const GameEntry& game);
    static DownloadRequest forApp(DownloadService& downloads, AppManager& apps, const AppEntry& app);
};

// Modal download screen (a ROM or a homebrew): starts the download, shows
// its progress from the EventBus, B cancels (the service deletes the
// staging file), then A/B closes once finished with a summary.
class DownloadActivity : public brls::Activity {
  public:
    DownloadActivity(DownloadService& downloads, EventBus& bus, DownloadRequest request);
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
    DownloadRequest request_;
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
