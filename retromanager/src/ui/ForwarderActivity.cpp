#include "retromanager/ui/ForwarderActivity.hpp"

#include <algorithm>

#include "retromanager/core/Format.hpp"
#include "retromanager/forwarder/nsp/Metadata.hpp"
#include "retromanager/models/Systems.hpp"

namespace rm::ui {

namespace {

std::string joined(const std::vector<std::string>& items) {
    std::string out;
    for (const auto& item : items) out += (out.empty() ? "" : ", ") + item;
    return out;
}

void setOptionalText(brls::Label* label, const std::string& text) {
    label->setText(text);
    label->setVisibility(text.empty() ? brls::Visibility::GONE : brls::Visibility::VISIBLE);
}

}  // namespace

std::string ForwarderActivity::describeIssue(const ForwarderReport& report) {
    SdLayout layout;
    switch (report.issue) {
        case ForwarderIssue::None: return "";
        case ForwarderIssue::KeysMissing: return brls::getStr("retromanager/forwarder/error_keys_missing", report.detail);
        case ForwarderIssue::KeysInvalid:
            return brls::getStr("retromanager/forwarder/error_keys_invalid", report.detail, layout.prodKeys);
        case ForwarderIssue::StubMissing:
            return brls::getStr("retromanager/forwarder/error_stub_missing", report.detail, joined(report.missingFiles));
        case ForwarderIssue::StubInvalid: return brls::getStr("retromanager/forwarder/error_stub_invalid", report.detail);
        case ForwarderIssue::RomMissing: return brls::getStr("retromanager/forwarder/error_rom_missing", report.detail);
        case ForwarderIssue::CoreMissing:
            return brls::getStr("retromanager/forwarder/error_core_missing", report.detail, joined(report.cores));
        case ForwarderIssue::WriteFailed: return brls::getStr("retromanager/forwarder/error_write", report.detail);
        case ForwarderIssue::Cancelled: return brls::getStr("retromanager/forwarder/cancelled");
        case ForwarderIssue::Failed: break;
    }
    return brls::getStr("retromanager/forwarder/error_generic", report.detail);
}

ForwarderActivity::ForwarderActivity(ForwarderTools tools, GameEntry game) : tools_(tools), game_(std::move(game)) {}

ForwarderActivity::~ForwarderActivity() {
    *alive_ = false;
    cancel_->cancel();  // the result of a build still running is dropped
}

void ForwarderActivity::onContentAvailable() {
    gameLabel->setText(game_.title + "  ·  " + systems::displayName(game_.system));
    setOptionalText(detailsLabel, "");
    root->registerAction(brls::getStr("retromanager/download/cancel"), brls::BUTTON_B,
                         [this](brls::View*) { return onBack(); }, false, false, brls::SOUND_BACK);
    actionButton->registerClickAction([this](brls::View*) { return onBack(); });
    brls::Application::giveFocus(actionButton);

    // Keys and stub first: no point waiting to learn a file is missing.
    ForwarderReport ready = tools_.builder.checkPrerequisites();
    if (!ready.ok()) {
        onBuilt(ready);
        return;
    }

    brls::Logger::info("Forwarder: building {} ({}, {})", game_.title, game_.system, game_.id);
    statusLabel->setText(brls::getStr("retromanager/forwarder/working", game_.title));
    setOptionalText(detailsLabel, brls::getStr("retromanager/forwarder/working_hint"));
    std::weak_ptr<bool> alive = alive_;
    ForwarderBuilder& builder = tools_.builder;
    ITaskRunner& runner = tools_.runner;
    GameEntry game = game_;
    std::shared_ptr<CancellationToken> cancel = cancel_;
    runner.runInBackground([this, alive, &builder, &runner, game, cancel] {
        ForwarderReport report = builder.build(game, *cancel);
        runner.runOnMainThread([this, alive, report] {
            auto flag = alive.lock();
            if (flag && *flag) onBuilt(report);
        });
    });
}

void ForwarderActivity::onBuilt(const ForwarderReport& report) {
    if (finished_) return;
    if (report.ok()) {
        brls::Logger::info("Forwarder: {} ({}) written to sdmc:{} ({}), core {}", report.title, nsp::titleIdHex(report.titleId),
                           report.nspPath, formatBytes(report.sizeBytes), report.corePath);
        std::string details = brls::getStr("retromanager/forwarder/done_details", "sdmc:" + report.nspPath + "  (" +
                                           formatBytes(report.sizeBytes) + ")", report.corePath, report.romPath,
                                           nsp::titleIdHex(report.titleId));
        if (report.placeholderIcon) details += "\n" + brls::getStr("retromanager/forwarder/placeholder_icon");
        details += "\n\n" + brls::getStr("retromanager/forwarder/sigpatches");
        finish(brls::getStr("retromanager/forwarder/done"), details);
        return;
    }
    const std::string message = describeIssue(report);
    if (report.issue == ForwarderIssue::Cancelled) {
        brls::Logger::info("Forwarder for {}: cancelled", game_.title);
        finish(brls::getStr("retromanager/forwarder/failed"), message);
        return;
    }
    std::string oneLine = message;
    std::replace(oneLine.begin(), oneLine.end(), '\n', ' ');
    brls::Logger::error("Forwarder for {} not created: {} [{}]", game_.title, oneLine, report.detail);
    finish(brls::getStr("retromanager/forwarder/failed"), message, true);

    // Blocking: the reason must not go unnoticed on a TV across the room.
    auto* dialog = new brls::Dialog(brls::getStr("retromanager/forwarder/failed") + "\n\n" + message + "\n\n" +
                                    brls::getStr("retromanager/forwarder/log_hint"));
    dialog->addButton(brls::getStr("retromanager/download/close"), [] {});
    dialog->open();
}

void ForwarderActivity::finish(const std::string& status, const std::string& details, bool error) {
    finished_ = true;
    spinner->setVisibility(brls::Visibility::GONE);
    brls::Theme theme = brls::Application::getTheme();
    statusLabel->setText(status);
    statusLabel->setTextColor(error ? nvgRGB(255, 90, 90) : theme.getColor("brls/accent"));
    setOptionalText(detailsLabel, details);
    detailsLabel->setTextColor(theme.getColor("brls/text"));  // the result must be readable, not greyed out
    actionButton->setText(brls::getStr("retromanager/download/close"));
    root->updateActionHint(brls::BUTTON_B, brls::getStr("retromanager/download/close"));
    root->setActionAvailable(brls::BUTTON_B, true);
}

bool ForwarderActivity::onBack() {
    if (closing_) return true;
    closing_ = true;
    if (!finished_) {
        cancel_->cancel();
        brls::Application::notify(brls::getStr("retromanager/forwarder/cancelled"));
    }
    brls::sync([] { brls::Application::popActivity(); });
    return true;
}

}  // namespace rm::ui
