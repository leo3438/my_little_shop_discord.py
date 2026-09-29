#pragma once

#include <borealis.hpp>
#include <memory>

#include "retromanager/core/EventBus.hpp"
#include "retromanager/network/SourceCatalog.hpp"

namespace rm::ui {

// The user's shops (NAS over FTP, web shops over HTTP), as read from
// config.json (hand-written sources included) plus what was ignored in it.
// A on a source: use it (the shop screen shows it from now on) or delete
// it. X opens the add form (SourceFormActivity). Changes apply immediately
// and are saved to config.json.
class SourcesActivity : public brls::Activity {
  public:
    // `configNotes`: config.json warnings, or why it could not be read.
    SourcesActivity(SourceCatalog& catalog, SourceRouter& router, EventBus& bus, std::string configNotes = "");

    CONTENT_FROM_XML_RES("activity/sources.xml");

    void onContentAvailable() override;

  private:
    void rebuild();
    void openActions(const std::string& name);
    void promptAdd();
    void changed(const Status& result, const std::string& okMessage);

    SourceCatalog& catalog_;
    SourceRouter& router_;
    EventBus& bus_;
    std::string configNotes_;

    BRLS_BIND(brls::Box, root, "sources/root");
    BRLS_BIND(brls::Label, hint, "sources/hint");
    BRLS_BIND(brls::Box, list, "sources/list");
};

}  // namespace rm::ui
