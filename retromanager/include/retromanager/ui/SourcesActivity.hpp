#pragma once

#include <borealis.hpp>
#include <memory>
#include <string>

#include "retromanager/core/EventBus.hpp"
#include "retromanager/network/SourceCatalog.hpp"

namespace rm::ui {

// The user's shops (NAS over FTP, web shops over HTTP), as read from
// config.json (hand-written sources included) plus what was ignored in it.
//
// Every source row has its own buttons, reached with Left / Right:
// Use, Edit (also Y), Test the connection (the full reason on failure,
// e.g. "cURL error 67 (Login denied)"), Delete (confirmed). "Add a source"
// (also X) opens SourceFormActivity. Only buttons take the focus, so the
// D-pad always lands on something visible. Changes apply immediately and
// are saved to config.json.
class SourcesActivity : public brls::Activity {
  public:
    // `configNotes`: config.json warnings, or why it could not be read.
    SourcesActivity(SourceCatalog& catalog, SourceRouter& router, EventBus& bus, std::string configNotes = "");
    ~SourcesActivity() override;

    CONTENT_FROM_XML_RES("activity/sources.xml");

    void onContentAvailable() override;

  private:
    void rebuild(const std::string& focusName = "");
    void openForm(const std::string& editName);
    void confirmDelete(const std::string& name);
    void testConnection(const std::string& name);
    void changed(const Status& result, const std::string& okMessage, const std::string& focusName = "");

    SourceCatalog& catalog_;
    SourceRouter& router_;
    EventBus& bus_;
    std::string configNotes_;
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);

    BRLS_BIND(brls::Box, root, "sources/root");
    BRLS_BIND(brls::Label, hint, "sources/hint");
    BRLS_BIND(brls::Box, list, "sources/list");
};

}  // namespace rm::ui
