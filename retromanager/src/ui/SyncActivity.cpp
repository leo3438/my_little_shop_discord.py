#include "retromanager/ui/SyncActivity.hpp"

#include <algorithm>

namespace rm::ui {

namespace {

std::string describeAction(SyncAction action) {
    switch (action) {
        case SyncAction::Upload: return brls::getStr("retromanager/sync/action_upload");
        case SyncAction::Download: return brls::getStr("retromanager/sync/action_download");
        case SyncAction::ConflictKeepLocal:
        case SyncAction::ConflictKeepRemote: return brls::getStr("retromanager/sync/action_conflict");
        case SyncAction::InSync: break;
    }
    return brls::getStr("retromanager/sync/action_in_sync");
}

std::string describeFailure(const Error& error) {
    switch (error.code) {
        case ErrorCode::NotConfigured: return brls::getStr("retromanager/sync/error_not_configured");
        case ErrorCode::NetworkError: return brls::getStr("retromanager/sync/error_network");
        case ErrorCode::AuthenticationFailed: return brls::getStr("retromanager/shop/error_auth");
        case ErrorCode::PermissionDenied: return brls::getStr("retromanager/sync/error_denied");
        default: return brls::getStr("retromanager/sync/error_generic");
    }
}

// An empty label still takes room in the layout: hide it instead.
void setOptionalText(brls::Label* label, const std::string& text) {
    label->setText(text);
    label->setVisibility(text.empty() ? brls::Visibility::GONE : brls::Visibility::VISIBLE);
}

std::string fileName(const std::string& path) {
    std::size_t slash = path.rfind('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

}  // namespace

SyncActivity::SyncActivity(CloudSyncService& sync, EventBus& bus) : sync_(sync), bus_(bus) {}

SyncActivity::~SyncActivity() {
    // Leaving the screen (or quitting the app) stops the sync between two files.
    if (state_ != State::Finished) sync_.cancel();
}

void SyncActivity::onContentAvailable() {
    setOptionalText(remoteLabel, sync_.isConfigured() ? sync_.localSavesDirectory() + "   <->   " + sync_.remoteBaseUrl() : "");
    statsLabel->setText(" ");  // keeps its line: the layout does not jump at the first file
    setOptionalText(detailsLabel, "");
    setProgress(0);

    root->registerAction(brls::getStr("retromanager/download/cancel"), brls::BUTTON_B,
                         [this](brls::View*) { return onBack(); }, false, false, brls::SOUND_BACK);
    actionButton->registerClickAction([this](brls::View*) { return onBack(); });
    brls::Application::giveFocus(actionButton);

    if (!sync_.isConfigured()) {
        track->setVisibility(brls::Visibility::GONE);
        statsLabel->setVisibility(brls::Visibility::GONE);
        finish(brls::getStr("retromanager/sync/error_not_configured"), "");
        return;
    }

    // Subscribe before starting: every event of this sync is seen.
    progressed_ = bus_.subscribe<SyncProgressed>([this](const SyncProgressed& e) { onProgress(e); });
    finished_ = bus_.subscribe<SyncFinished>([this](const SyncFinished& e) { onFinished(e); });
    if (!sync_.start()) {
        // The previous one (cancelled when its screen closed) has not stopped yet.
        progressed_ = {};
        finished_ = {};
        finish(brls::getStr("retromanager/sync/already_running"), "");
        return;
    }
    statusLabel->setText(brls::getStr("retromanager/sync/scanning"));
}

void SyncActivity::onProgress(const SyncProgressed& event) {
    if (state_ != State::Running) return;
    setProgress(event.total > 0 ? static_cast<double>(event.done) / event.total : 1.0);
    statsLabel->setText(std::to_string(event.done) + " / " + std::to_string(event.total));
    statusLabel->setText(describeAction(event.action) + " : " + event.path);
}

void SyncActivity::onFinished(const SyncFinished& event) {
    if (state_ == State::Finished) return;
    if (!event.result.ok() && event.result.error().code == ErrorCode::Cancelled) {
        brls::Logger::info("Cloud sync cancelled");
        brls::Application::notify(brls::getStr("retromanager/sync/cancelled"));
        state_ = State::Finished;
        close();
        return;
    }
    if (!event.result.ok()) {
        brls::Logger::error("Cloud sync failed: {}", event.result.error().describe());
        finish(describeFailure(event.result.error()), event.result.error().message);
        return;
    }

    const SyncReport& report = event.report;
    brls::Logger::info("Cloud sync: {} sent, {} received, {} unchanged, {} conflicts, {} failures", report.uploaded,
                       report.downloaded, report.unchanged, report.conflicts, report.failures.size());
    std::string details = brls::getStr("retromanager/sync/unchanged", std::to_string(report.unchanged));
    if (report.conflicts > 0) {
        details += "\n" + brls::getStr("retromanager/sync/conflicts", std::to_string(report.conflicts));
        for (const std::string& copy : report.conflictCopies) details += "\n  " + fileName(copy);
    }
    if (!report.failures.empty()) {
        details += "\n" + brls::getStr("retromanager/sync/failures", std::to_string(report.failures.size()));
        for (const auto& [path, error] : report.failures) {
            brls::Logger::warning("Cloud sync {}: {}", path, error.describe());
            details += "\n  " + path + " : " + error.message;
        }
    }
    setProgress(1.0);
    finish(brls::getStr("retromanager/sync/done", std::to_string(report.uploaded), std::to_string(report.downloaded)),
           details);
}

void SyncActivity::finish(const std::string& status, const std::string& details) {
    state_ = State::Finished;
    statusLabel->setText(status);
    setOptionalText(detailsLabel, details);
    actionButton->setText(brls::getStr("retromanager/download/close"));
    root->updateActionHint(brls::BUTTON_B, brls::getStr("retromanager/download/close"));
    root->setActionAvailable(brls::BUTTON_B, true);  // refreshes the hints bar
}

bool SyncActivity::onBack() {
    switch (state_) {
        case State::Running:
            state_ = State::Cancelling;
            statusLabel->setText(brls::getStr("retromanager/sync/cancelling"));
            sync_.cancel();  // SyncFinished{Cancelled} follows and closes the screen
            break;
        case State::Cancelling: break;
        case State::Finished: close(); break;
    }
    return true;
}

void SyncActivity::setProgress(double ratio) {
    float trackWidth = track->getWidth() > 0 ? track->getWidth() : 800.0f;
    fill->setWidth(static_cast<float>(std::clamp(ratio, 0.0, 1.0)) * trackWidth);
}

void SyncActivity::close() {
    if (closing_) return;
    closing_ = true;
    brls::sync([] { brls::Application::popActivity(); });
}

}  // namespace rm::ui
