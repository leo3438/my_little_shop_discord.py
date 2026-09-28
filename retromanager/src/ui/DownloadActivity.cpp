#include "retromanager/ui/DownloadActivity.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

#include "retromanager/core/Format.hpp"
#include "retromanager/models/Systems.hpp"

namespace rm::ui {

namespace {

std::string describeFailure(const DownloadFinished& event) {
    const Error& error = event.result.error();
    switch (error.code) {
        case ErrorCode::InsufficientSpace:
            return brls::getStr("retromanager/download/error_space", formatBytes(event.space.requiredBytes),
                                formatBytes(event.space.availableBytes.value_or(0)));
        case ErrorCode::NetworkError: return brls::getStr("retromanager/download/error_network");
        case ErrorCode::AuthenticationFailed: return brls::getStr("retromanager/shop/error_auth");
        case ErrorCode::NotFound: return brls::getStr("retromanager/download/error_not_found");
        case ErrorCode::IntegrityError: return brls::getStr("retromanager/download/error_integrity");
        case ErrorCode::PermissionDenied: return brls::getStr("retromanager/download/error_denied");
        case ErrorCode::NotConfigured: return brls::getStr("retromanager/shop/error_not_configured");
        default: return brls::getStr("retromanager/download/error_generic");
    }
}

// One line per post-install step outcome.
std::string describeStep(const StepOutcome& step, const std::string& romFolder, const GameEntry& game) {
    const bool ok = step.result.ok();
    const bool missing = !ok && step.result.error().code == ErrorCode::NotFound;
    if (step.id == "retroarch") {
        if (ok) return brls::getStr("retromanager/download/step_retroarch_ok", romFolder);
        if (missing) return brls::getStr("retromanager/download/step_retroarch_missing");
        return brls::getStr("retromanager/download/step_retroarch_failed", step.result.error().describe());
    }
    if (step.id == "playlist") {
        const SystemInfo* system = systems::find(game.system);
        if (ok) return brls::getStr("retromanager/download/step_playlist_ok", system ? system->libretroName : game.system);
        if (missing) return brls::getStr("retromanager/download/step_playlist_missing");
        return brls::getStr("retromanager/download/step_playlist_failed", step.result.error().describe());
    }
    if (step.id == "boxart") {
        if (ok) return brls::getStr("retromanager/download/step_boxart_ok");
        if (missing) return brls::getStr("retromanager/download/step_boxart_missing");
        return brls::getStr("retromanager/download/step_boxart_failed", step.result.error().describe());
    }
    if (step.id == "cheats") {
        if (ok) return brls::getStr("retromanager/download/step_cheats_ok");
        if (missing) return brls::getStr("retromanager/download/step_cheats_missing");
        return brls::getStr("retromanager/download/step_cheats_failed", step.result.error().describe());
    }
    if (step.id == "sysclk") {
        if (ok) return brls::getStr("retromanager/download/step_sysclk_ok");
        if (missing) return brls::getStr("retromanager/download/step_sysclk_missing");
        return brls::getStr("retromanager/download/step_sysclk_failed", step.result.error().describe());
    }
    return step.id + ": " + (ok ? "OK" : step.result.error().describe());
}

std::string parentFolder(const std::string& path) {
    std::size_t slash = path.rfind('/');
    return slash == std::string::npos ? path : path.substr(0, slash + 1);
}

}  // namespace

DownloadActivity::DownloadActivity(DownloadService& downloads, EventBus& bus, GameEntry game)
    : downloads_(downloads), bus_(bus), game_(std::move(game)) {}

DownloadActivity::~DownloadActivity() {
    // Leaving the screen (or quitting the app) never leaves a transfer running.
    if (state_ != State::Finished) downloads_.cancel(id_);
}

void DownloadActivity::onContentAvailable() {
    titleLabel->setText(game_.title);
    destinationLabel->setText("");
    statsLabel->setText(game_.sizeBytes > 0 ? formatBytes(game_.sizeBytes) : "");
    statusLabel->setText(brls::getStr("retromanager/download/waiting"));
    stepsLabel->setText("");
    setProgress(0);

    // B cancels while running, closes once finished. Registered on the
    // content root so it wins over the AppletFrame's own "back" action.
    root->registerAction(brls::getStr("retromanager/download/cancel"), brls::BUTTON_B,
                         [this](brls::View*) { return onBack(); }, false, false, brls::SOUND_BACK);
    actionButton->registerClickAction([this](brls::View*) { return onBack(); });
    brls::Application::giveFocus(actionButton);

    // Subscribe before starting: every event of this download is seen.
    started_ = bus_.subscribe<DownloadStarted>([this](const DownloadStarted& e) { onStarted(e); });
    progressed_ = bus_.subscribe<DownloadProgressed>([this](const DownloadProgressed& e) { onProgress(e); });
    configuring_ = bus_.subscribe<DownloadConfiguring>([this](const DownloadConfiguring& e) { onConfiguring(e); });
    finished_ = bus_.subscribe<DownloadFinished>([this](const DownloadFinished& e) { onFinished(e); });
    id_ = downloads_.start(game_);
}

void DownloadActivity::onStarted(const DownloadStarted& event) {
    if (event.id != id_) return;
    destinationLabel->setText(event.destination);
    if (state_ == State::Running) statusLabel->setText(brls::getStr("retromanager/download/running"));
}

void DownloadActivity::onProgress(const DownloadProgressed& event) {
    if (event.id != id_ || state_ != State::Running) return;
    std::string speed = formatBytes(static_cast<std::uint64_t>(std::llround(event.bytesPerSecond))) + "/s";
    if (event.total > 0) {
        double ratio = std::min(1.0, static_cast<double>(event.received) / static_cast<double>(event.total));
        setProgress(ratio);
        statsLabel->setText(std::to_string(static_cast<int>(ratio * 100.0)) + " %   ·   " + formatBytes(event.received) +
                            " / " + formatBytes(event.total) + "   ·   " + speed);
    } else {
        statsLabel->setText(formatBytes(event.received) + "   ·   " + speed);
    }
}

void DownloadActivity::onConfiguring(const DownloadConfiguring& event) {
    if (event.id != id_) return;
    // The ROM is installed: cancelling now would only skip the extras.
    state_ = State::Configuring;
    setProgress(1.0);
    statusLabel->setText(brls::getStr("retromanager/download/configuring"));
    root->setActionAvailable(brls::BUTTON_B, false);  // also refreshes the hints bar
    actionButton->setState(brls::ButtonState::DISABLED);
    actionButton->setText(brls::getStr("retromanager/download/please_wait"));  // the bordered style hides DISABLED
}

void DownloadActivity::onFinished(const DownloadFinished& event) {
    if (event.id != id_) return;
    state_ = State::Finished;
    actionButton->setText(brls::getStr("retromanager/download/close"));
    root->updateActionHint(brls::BUTTON_B, brls::getStr("retromanager/download/close"));
    root->setActionAvailable(brls::BUTTON_B, true);  // re-enabled after configuring; refreshes the hints bar
    actionButton->setState(brls::ButtonState::ENABLED);

    if (event.result.ok()) {
        brls::Logger::info("Installed {} -> {}", game_.title, event.destination);
        setProgress(1.0);
        statusLabel->setText(brls::getStr("retromanager/download/done"));
        std::string report;
        for (const StepOutcome& step : event.steps) {
            if (!step.result.ok()) brls::Logger::warning("Post-install {}: {}", step.id, step.result.error().describe());
            if (!report.empty()) report += "\n";
            report += describeStep(step, parentFolder(event.destination), game_);
        }
        stepsLabel->setText(report);
        return;
    }
    if (event.result.error().code == ErrorCode::Cancelled) {
        brls::Logger::info("Download cancelled: {}", game_.title);
        brls::Application::notify(brls::getStr("retromanager/download/cancelled", game_.title));
        close();
        return;
    }
    brls::Logger::error("Download failed: {}: {}", game_.title, event.result.error().describe());
    statusLabel->setText(describeFailure(event));
    statsLabel->setText(event.result.error().message);
}

bool DownloadActivity::onBack() {
    switch (state_) {
        case State::Running:
            state_ = State::Cancelling;
            statusLabel->setText(brls::getStr("retromanager/download/cancelling"));
            downloads_.cancel(id_);  // DownloadFinished{Cancelled} follows and closes the screen
            break;
        case State::Cancelling:
        case State::Configuring: break;
        case State::Finished: close(); break;
    }
    return true;
}

void DownloadActivity::setProgress(double ratio) {
    // Pixel width from the track: a percentage width would be resolved
    // against another ancestor by the layout engine.
    float trackWidth = track->getWidth() > 0 ? track->getWidth() : 800.0f;
    fill->setWidth(static_cast<float>(std::clamp(ratio, 0.0, 1.0)) * trackWidth);
}

void DownloadActivity::close() {
    if (closing_) return;  // one pop only, even if B is pressed again
    closing_ = true;
    // Deferred: this may run from inside one of our own event handlers.
    brls::sync([] { brls::Application::popActivity(); });
}

}  // namespace rm::ui
