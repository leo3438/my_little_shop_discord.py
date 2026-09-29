#include "retromanager/ui/SourceFormActivity.hpp"

#include "retromanager/core/Format.hpp"

namespace rm::ui {

namespace {

// swkbd writes into a 0x100-byte buffer (UTF-8): stay below it.
constexpr int kMaxUrl = 200;
// What fits on one line of a field at font size 22.
constexpr std::size_t kCharsPerLine = 80;

std::string typeName(const std::string& type) {
    if (type == "http") return brls::getStr("retromanager/sources/type_http");
    if (type == "ftp") return brls::getStr("retromanager/sources/type_ftp");
    return "";
}

}  // namespace

// --- FormField ---------------------------------------------------------------

FormField::FormField(std::string label, std::string placeholder, std::string hint, int maxLength, bool secret)
    : label_(std::move(label)),
      placeholder_(std::move(placeholder)),
      hint_(std::move(hint)),
      maxLength_(maxLength),
      secret_(secret) {
    setAxis(brls::Axis::COLUMN);
    setFocusable(true);
    setPadding(14, 16, 14, 16);
    setMarginBottom(10);
    setCornerRadius(6);
    setBackgroundColor(brls::Application::getTheme().getColor("brls/backdrop"));

    auto* title = new brls::Label();
    title->setText(label_);
    title->setFontSize(20);
    addView(title);

    valueLabel_ = new brls::Label();
    valueLabel_->setFontSize(22);
    valueLabel_->setMarginTop(6);
    // Whole width, several lines, no ticker animation: the full value stays readable.
    valueLabel_->setWidthPercentage(100);
    valueLabel_->setSingleLine(false);
    valueLabel_->setAutoAnimate(false);
    addView(valueLabel_);

    if (!hint_.empty()) {
        auto* help = new brls::Label();
        help->setText(hint_);
        help->setFontSize(15);
        help->setMarginTop(6);
        help->setTextColor(brls::Application::getTheme().getColor("brls/text_disabled"));
        addView(help);
    }
    refresh();
    registerClickAction([this](brls::View*) {
        edit();
        return true;
    });
    addGestureRecognizer(new brls::TapGestureRecognizer(this));
}

void FormField::setValue(std::string value) {
    value_ = std::move(value);
    refresh();
    if (onChanged) onChanged();
}

void FormField::edit() {
    // Header = the field's label, sub text = the example: the console
    // keyboard itself says what is being typed; the value is pre-filled.
    brls::Application::getImeManager()->openForText(
        [this](std::string text) { brls::sync([this, text] { setValue(text); }); }, label_, placeholder_, maxLength_,
        value_);
}

void FormField::refresh() {
    brls::Theme theme = brls::Application::getTheme();
    if (value_.empty()) {
        valueLabel_->setText(placeholder_);
        valueLabel_->setTextColor(theme.getColor("brls/text_disabled"));
    } else if (secret_) {
        valueLabel_->setText(std::string(std::min<std::size_t>(value_.size(), 24), '*') + "   " +
                             brls::getStr("retromanager/sources/password_mask", std::to_string(value_.size())));
        valueLabel_->setTextColor(theme.getColor("brls/text"));
    } else {
        valueLabel_->setText(breakLongLines(value_, kCharsPerLine));
        valueLabel_->setTextColor(theme.getColor("brls/text"));
    }
}

// --- SourceFormActivity -------------------------------------------------------

SourceFormActivity::SourceFormActivity(SourceCatalog& catalog, std::function<void(const std::string&)> onAdded)
    : catalog_(catalog), onAdded_(std::move(onAdded)) {}

void SourceFormActivity::onContentAvailable() {
    name_ = new FormField(brls::getStr("retromanager/sources/field_name"), brls::getStr("retromanager/sources/placeholder_name"),
                          brls::getStr("retromanager/sources/field_name_hint"), 32);
    url_ = new FormField(brls::getStr("retromanager/sources/field_url"), brls::getStr("retromanager/sources/placeholder_url"),
                         brls::getStr("retromanager/sources/field_url_hint"), kMaxUrl);
    user_ = new FormField(brls::getStr("retromanager/sources/field_user"), brls::getStr("retromanager/sources/placeholder_user"),
                          "", 64);
    password_ = new FormField(brls::getStr("retromanager/sources/field_password"),
                              brls::getStr("retromanager/sources/placeholder_password"), "", 128, true);
    // Above the type line, the error line and the buttons.
    std::size_t at = 0;
    for (FormField* field : {name_, url_, user_, password_}) {
        fields->addView(field, at++);
        field->onChanged = [this] {
            errorLabel->setVisibility(brls::Visibility::GONE);
            updateType();
        };
    }
    updateType();

    saveButton->registerClickAction([this](brls::View*) {
        save();
        return true;
    });
    cancelButton->registerClickAction([](brls::View*) {
        brls::Application::popActivity();
        return true;
    });
    brls::Application::giveFocus(name_);
}

void SourceFormActivity::updateType() {
    const std::string url = url_->value();
    const std::string type = SourceCatalog::typeForUrl(url);
    if (url.empty()) {
        typeLabel->setText(brls::getStr("retromanager/sources/detected_empty"));
    } else if (type.empty()) {
        typeLabel->setText(brls::getStr("retromanager/sources/detected_none"));
    } else {
        typeLabel->setText(brls::getStr("retromanager/sources/detected", typeName(type)));
    }
}

void SourceFormActivity::showError(const std::string& text) {
    errorLabel->setText(text);
    errorLabel->setVisibility(brls::Visibility::VISIBLE);
}

void SourceFormActivity::save() {
    if (name_->value().find_first_not_of(" \t") == std::string::npos) {
        showError(brls::getStr("retromanager/sources/missing_name"));
        brls::Application::giveFocus(name_);
        return;
    }
    if (url_->value().find_first_not_of(" \t") == std::string::npos) {
        showError(brls::getStr("retromanager/sources/missing_url"));
        brls::Application::giveFocus(url_);
        return;
    }
    ShopConfig draft;
    draft.type.clear();  // deduced from the address
    draft.name = name_->value();
    draft.url = url_->value();
    draft.username = user_->value();
    draft.password = password_->value();
    Status added = catalog_.add(draft);
    if (!added) {
        brls::Logger::error("Sources: {}", added.error().describe());
        showError(brls::getStr("retromanager/sources/error", added.error().message));
        return;
    }
    brls::Logger::info("Sources: added {} ({})", draft.name, draft.url);
    std::string name = catalog_.config().sources.back().name;
    auto done = onAdded_;
    brls::sync([done, name] {
        brls::Application::popActivity(brls::TransitionAnimation::FADE, [done, name] {
            if (done) done(name);
        });
    });
}

}  // namespace rm::ui
