#pragma once

#include <borealis.hpp>
#include <functional>
#include <optional>
#include <string>

#include "retromanager/models/AppConfig.hpp"
#include "retromanager/network/SourceCatalog.hpp"

namespace rm::ui {

// One labelled field of a form: the label, then the value on its own lines
// (wrapped, never cut) or a grey placeholder when empty, then a hint. A
// opens the console keyboard with the label as header and the current value
// already typed, so the user always sees what they enter.
//
// Focus: the field is a ROW box (like Borealis' own cells) holding a
// non-focusable column of labels. Borealis asks the focused view itself for
// the next focus; a focusable COLUMN box would first walk its own children
// with its index in the parent, which made D-pad navigation erratic.
class FormField : public brls::Box {
  public:
    FormField(std::string label, std::string placeholder, std::string hint, int maxLength, bool secret = false);

    const std::string& value() const { return value_; }
    void setValue(std::string value);
    // Called after each edit.
    std::function<void()> onChanged;

  private:
    void edit();
    void refresh();

    std::string label_, placeholder_, hint_;
    std::string value_;
    int maxLength_;
    bool secret_;
    brls::Label* valueLabel_;
};

// "Add a source" / "Edit a source" form: Name, index URL, user name,
// password, the type deduced from the URL, Save / Cancel. Validation errors
// show in the form, which stays open with what was typed. Editing replaces
// the existing entry of config.json (same place, still active if it was).
class SourceFormActivity : public brls::Activity {
  public:
    // `existing`: the source to edit (fields pre-filled); nullopt to add one.
    SourceFormActivity(SourceCatalog& catalog, std::function<void(const std::string& name)> onSaved,
                       std::optional<ShopConfig> existing = std::nullopt);

    CONTENT_FROM_XML_RES("activity/source_form.xml");

    void onContentAvailable() override;

  private:
    void updateType();
    void save();
    void showError(const std::string& text);

    SourceCatalog& catalog_;
    std::function<void(const std::string&)> onSaved_;
    std::optional<ShopConfig> existing_;
    FormField* name_ = nullptr;
    FormField* url_ = nullptr;
    FormField* user_ = nullptr;
    FormField* password_ = nullptr;

    BRLS_BIND(brls::Box, root, "form/root");
    BRLS_BIND(brls::Box, fields, "form/fields");
    BRLS_BIND(brls::Label, typeLabel, "form/type");
    BRLS_BIND(brls::Label, errorLabel, "form/error");
    BRLS_BIND(brls::Button, saveButton, "form/save");
    BRLS_BIND(brls::Button, cancelButton, "form/cancel");
};

}  // namespace rm::ui
