#include "retromanager/ui/DownloadsActivity.hpp"

#include <algorithm>

#include "retromanager/core/Format.hpp"
#include "retromanager/ui/DownloadSummary.hpp"

namespace rm::ui {

namespace {

const NVGcolor kOk = nvgRGB(46, 204, 113);
const NVGcolor kFailed = nvgRGB(231, 76, 60);

bool isInside(brls::View* view, brls::View* container) {
    for (brls::View* v = view; v != nullptr; v = v->getParent()) {
        if (v == container) return true;
    }
    return false;
}

}  // namespace

DownloadsActivity::DownloadsActivity(DownloadQueueManager& queue, EventBus& bus) : queue_(queue), bus_(bus) {}

void DownloadsActivity::onContentAvailable() {
    currentHeader->setTitle(brls::getStr("retromanager/downloads/current"));
    pendingEmpty->setText(brls::getStr("retromanager/downloads/pending_empty"));
    finishedEmpty->setText(brls::getStr("retromanager/downloads/finished_empty"));

    root->registerAction(brls::getStr("retromanager/downloads/cancel_current"), brls::BUTTON_X, [this](brls::View*) {
        if (runningId_ != 0) {
            queue_.cancel(runningId_);  // DownloadFinished{Cancelled} follows: the partial file is deleted
            currentStats->setText(brls::getStr("retromanager/download/cancelling"));
        }
        return true;
    });

    // Subscribe first, then read the current state: nothing is missed.
    changed_ = bus_.subscribe<DownloadQueueChanged>([this](const DownloadQueueChanged& e) { onQueueChanged(e.items); });
    progressed_ = bus_.subscribe<DownloadProgressed>([this](const DownloadProgressed& e) {
        if (e.id != runningId_) return;
        QueueItem item;
        item.received = e.received;
        item.total = e.total;
        item.bytesPerSecond = e.bytesPerSecond;
        item.resumedFrom = e.resumedFrom;
        showProgress(item);
    });
    configuring_ = bus_.subscribe<DownloadConfiguring>([this](const DownloadConfiguring& e) {
        if (e.id != runningId_) return;
        setProgress(1.0);
        currentStats->setText(brls::getStr("retromanager/download/configuring"));
    });
    finished_ = bus_.subscribe<DownloadFinished>([this](const DownloadFinished&) { rebuildFinished(); });

    onQueueChanged(queue_.snapshot());
    rebuildFinished();
    brls::Application::giveFocus(current);
}

void DownloadsActivity::onQueueChanged(const std::vector<QueueItem>& items) {
    const QueueItem* running = !items.empty() && items[0].state == QueueItemState::Running ? &items[0] : nullptr;
    if (!shown_ || (running ? running->id : 0) != runningId_) showCurrent(running);

    std::vector<QueueItem> waiting(items.begin() + (running ? 1 : 0), items.end());
    std::vector<DownloadId> ids;
    for (const QueueItem& item : waiting) ids.push_back(item.id);
    if (!shown_ || ids != pendingIds_) {
        pendingIds_ = ids;
        rebuildPending(waiting);
    }
    root->setActionAvailable(brls::BUTTON_X, runningId_ != 0);
    shown_ = true;
}

void DownloadsActivity::showCurrent(const QueueItem* running) {
    runningId_ = running ? running->id : 0;
    track->setVisibility(running ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
    currentDestination->setVisibility(running ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
    if (!running) {
        currentTitle->setText(brls::getStr("retromanager/downloads/idle"));
        currentStats->setText("");
        return;
    }
    currentTitle->setText(running->title);
    currentDestination->setText(running->kind == DownloadKind::App ? brls::getStr("retromanager/downloads/kind_app")
                                                                   : brls::getStr("retromanager/downloads/kind_rom"));
    if (running->received > 0) {
        showProgress(*running);
    } else {
        setProgress(0);
        currentStats->setText(brls::getStr("retromanager/download/waiting"));
    }
}

void DownloadsActivity::showProgress(const QueueItem& item) {
    if (item.total > 0) setProgress(static_cast<double>(item.received) / static_cast<double>(item.total));
    currentStats->setText(progressLine(item));
}

void DownloadsActivity::rebuildPending(const std::vector<QueueItem>& items) {
    pendingHeader->setTitle(brls::getStr("retromanager/downloads/pending", std::to_string(items.size())));
    pendingEmpty->setVisibility(items.empty() ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
    rebuild(pending, [this, &items] {
        for (const QueueItem& item : items) {
            auto* cell = new brls::DetailCell();
            cell->setText(item.title);
            std::string detail = item.sizeBytes > 0 ? formatBytes(item.sizeBytes) + "  ·  " : "";
            cell->setDetailText(detail + brls::getStr("retromanager/downloads/remove_hint"));
            cell->setDetailTextColor(brls::Application::getTheme().getColor("brls/text_disabled"));
            const DownloadId id = item.id;
            const std::string title = item.title;
            cell->registerClickAction([this, id, title](brls::View*) {
                if (queue_.cancel(id)) brls::Application::notify(brls::getStr("retromanager/downloads/removed", title));
                return true;  // the QueueChanged event rebuilds the list
            });
            pending->addView(cell);
        }
    });
}

void DownloadsActivity::rebuildFinished() {
    std::vector<DownloadOutcome> history = queue_.history();
    DownloadId newest = history.empty() ? 0 : history.front().id;
    if (newest == newestFinished_ && newest != 0) return;
    newestFinished_ = newest;

    finishedHeader->setTitle(brls::getStr("retromanager/downloads/finished", std::to_string(history.size())));
    finishedEmpty->setVisibility(history.empty() ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
    rebuild(finished, [this, &history] {
        for (const DownloadOutcome& outcome : history) {
            auto* cell = new brls::DetailCell();
            cell->setText(outcome.title);
            const bool ok = outcome.result.ok();
            const bool cancelled = !ok && outcome.result.error().code == ErrorCode::Cancelled;
            cell->setDetailText(ok ? brls::getStr("retromanager/downloads/state_done")
                                   : cancelled ? brls::getStr("retromanager/downloads/cancelled")
                                               : brls::getStr("retromanager/downloads/state_failed"));
            cell->setDetailTextColor(ok ? kOk
                                        : cancelled ? brls::Application::getTheme().getColor("brls/text_disabled")
                                                    : kFailed);
            std::string text = outcome.title + "\n\n" + outcomeHeadline(outcome);
            if (std::string details = outcomeDetails(outcome); !details.empty()) text += "\n\n" + details;
            cell->registerClickAction([text](brls::View*) {
                auto* dialog = new brls::Dialog(text);
                dialog->addButton(brls::getStr("retromanager/download/close"), [] {});
                dialog->open();
                return true;
            });
            finished->addView(cell);
        }
    });
}

void DownloadsActivity::rebuild(brls::Box* list, const std::function<void()>& refill) {
    int focusedRow = -1;
    brls::View* focus = brls::Application::getCurrentFocus();
    if (focus != nullptr && isInside(focus, list)) {
        auto& rows = list->getChildren();
        for (std::size_t i = 0; i < rows.size(); ++i) {
            if (isInside(focus, rows[i])) focusedRow = static_cast<int>(i);
        }
        brls::Application::giveFocus(current);  // never leave the focus on a view about to be deleted
    }
    list->clearViews();
    refill();
    auto& rows = list->getChildren();
    if (focusedRow >= 0 && !rows.empty()) {
        brls::Application::giveFocus(rows[std::min<std::size_t>(static_cast<std::size_t>(focusedRow), rows.size() - 1)]);
    }
}

void DownloadsActivity::setProgress(double ratio) {
    float trackWidth = track->getWidth() > 0 ? track->getWidth() : 800.0f;
    fill->setWidth(static_cast<float>(std::clamp(ratio, 0.0, 1.0)) * trackWidth);
}

}  // namespace rm::ui
