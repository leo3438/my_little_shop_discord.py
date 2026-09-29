#pragma once

#include <borealis.hpp>
#include <memory>

#include "retromanager/core/ITaskRunner.hpp"
#include "retromanager/models/GameEntry.hpp"
#include "retromanager/services/ForwarderBuilder.hpp"

namespace rm::ui {

// Everything the games list needs to offer "Create a shortcut".
struct ForwarderTools {
    ForwarderBuilder& builder;
    ITaskRunner& runner;  // a WorkerThread: the build takes a few seconds
};

// Modal "Create a shortcut (Forwarder)" screen for an installed game: a
// spinner while the NSP is built in the background, then the summary
// (where the file is, how to install it) or an error saying exactly which
// file is missing and where to put it. B cancels, then closes.
class ForwarderActivity : public brls::Activity {
  public:
    ForwarderActivity(ForwarderTools tools, GameEntry game);
    ~ForwarderActivity() override;

    CONTENT_FROM_XML_RES("activity/forwarder.xml");

    void onContentAvailable() override;

    // The user-facing explanation of a failed report.
    static std::string describeIssue(const ForwarderReport& report);

  private:
    void onBuilt(const ForwarderReport& report);
    void finish(const std::string& status, const std::string& details, bool error = false);
    bool onBack();

    ForwarderTools tools_;
    GameEntry game_;
    bool finished_ = false;
    bool closing_ = false;
    std::shared_ptr<CancellationToken> cancel_ = std::make_shared<CancellationToken>();
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);

    BRLS_BIND(brls::Box, root, "forwarder/root");
    BRLS_BIND(brls::Label, gameLabel, "forwarder/game");
    BRLS_BIND(brls::ProgressSpinner, spinner, "forwarder/spinner");
    BRLS_BIND(brls::Label, statusLabel, "forwarder/status");
    BRLS_BIND(brls::Label, detailsLabel, "forwarder/details");
    BRLS_BIND(brls::Button, actionButton, "forwarder/action");
};

}  // namespace rm::ui
