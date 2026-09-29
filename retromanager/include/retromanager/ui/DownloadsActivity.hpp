#pragma once

#include <borealis.hpp>
#include <vector>

#include "retromanager/core/EventBus.hpp"
#include "retromanager/services/DownloadQueueManager.hpp"

namespace rm::ui {

// The download queue: the running download (progress, speed, X cancels it
// and deletes its partial file), the pending ones (A removes one), and the
// recent results (A shows the summary: playlist, box art, cheats...).
//
// Views are only rebuilt when the set of items changes; progress events
// update labels in place, so the queue can loop in the background without
// the screen stuttering or losing the focus.
class DownloadsActivity : public brls::Activity {
  public:
    DownloadsActivity(DownloadQueueManager& queue, EventBus& bus);

    CONTENT_FROM_XML_RES("activity/downloads.xml");

    void onContentAvailable() override;

  private:
    void onQueueChanged(const std::vector<QueueItem>& items);
    void showCurrent(const QueueItem* running);
    void showProgress(const QueueItem& item);
    void rebuildPending(const std::vector<QueueItem>& items);
    void rebuildFinished();
    // Clears `list` and refills it with `fill`, keeping the focus on the
    // same row when it was in there.
    void rebuild(brls::Box* list, const std::function<void()>& fill);
    void setProgress(double ratio);

    DownloadQueueManager& queue_;
    EventBus& bus_;
    bool shown_ = false;  // the first update renders everything
    DownloadId runningId_ = 0;
    std::vector<DownloadId> pendingIds_;
    DownloadId newestFinished_ = 0;
    EventBus::Subscription changed_, progressed_, configuring_, finished_;

    BRLS_BIND(brls::Box, root, "downloads/root");
    BRLS_BIND(brls::Header, currentHeader, "downloads/current_header");
    BRLS_BIND(brls::Box, current, "downloads/current");
    BRLS_BIND(brls::Label, currentTitle, "downloads/current_title");
    BRLS_BIND(brls::Label, currentDestination, "downloads/current_destination");
    BRLS_BIND(brls::Box, track, "downloads/track");
    BRLS_BIND(brls::Rectangle, fill, "downloads/fill");
    BRLS_BIND(brls::Label, currentStats, "downloads/current_stats");
    BRLS_BIND(brls::Header, pendingHeader, "downloads/pending_header");
    BRLS_BIND(brls::Box, pending, "downloads/pending");
    BRLS_BIND(brls::Label, pendingEmpty, "downloads/pending_empty");
    BRLS_BIND(brls::Header, finishedHeader, "downloads/finished_header");
    BRLS_BIND(brls::Box, finished, "downloads/finished");
    BRLS_BIND(brls::Label, finishedEmpty, "downloads/finished_empty");
};

}  // namespace rm::ui
