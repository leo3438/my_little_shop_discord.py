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
    if (type == "smb") return brls::getStr("retromanager/sources/type_smb");
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
    setAxis(brls::Axis::ROW);
    setFocusable(true);
    setPadding(14, 16, 14, 16);
    setMarginBottom(10);
    setCornerRadius(6);
    setBackgroundColor(brls::Application::getTheme().getColor("brls/backdrop"));

    auto* column = new brls::Box(brls::Axis::COLUMN);  // not focusable: only for layout
    column->setGrow(1.0f);
    addView(column);

    auto* title = new brls::Label();
    title->setText(label_);
    title->setFontSize(20);
    column->addView(title);

    valueLabel_ = new brls::Label();
    valueLabel_->setFontSize(22);
    valueLabel_->setMarginTop(6);
    // Whole width, several lines, no ticker animation: the full value stays readable.
    valueLabel_->setWidthPercentage(100);
    valueLabel_->setSingleLine(false);
    valueLabel_->setAutoAnimate(false);
    column->addView(valueLabel_);

    if (!hint_.empty()) {
        auto* help = new brls::Label();
        help->setText(hint_);
        help->setFontSize(15);
        help->setMarginTop(6);
        help->setTextColor(brls::Application::getTheme().getColor("brls/text_disabled"));
        column->addView(help);
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

SourceFormActivity::SourceFormActivity(SourceCatalog& catalog, std::function<void(const std::string&)> onSaved,
                                       std::optional<ShopConfig> existing)
    : catalog_(catalog), onSaved_(std::move(onSaved)), existing_(std::move(existing)) {}

void SourceFormActivity::onContentAvailable() {
    name_ = new FormField(brls::getStr("retromanager/sources/field_name"), brls::getStr("retromanager/sources/placeholder_name"),
                          brls::getStr("retromanager/sources/field_name_hint"), 32);
    url_ = new FormField(brls::getStr("retromanager/sources/field_url"), brls::getStr("retromanager/sources/placeholder_url"),
                         brls::getStr("retromanager/sources/field_url_hint"), kMaxUrl);
    user_ = new FormField(brls::getStr("retromanager/sources/field_user"), brls::getStr("retromanager/sources/placeholder_user"),
                          "", 64);
    password_ = new FormField(brls::getStr("retromanager/sources/field_password"),
                              brls::getStr("retromanager/sources/placeholder_password"), "", 128, true);
    if (existing_) {
        if (auto* frame = dynamic_cast<brls::AppletFrame*>(getContentView())) {
            frame->setTitle(brls::getStr("retromanager/sources/form_title_edit"));
        }
        name_->setValue(existing_->name);
        url_->setValue(existing_->url);
        user_->setValue(existing_->username);
        password_->setValue(existing_->password);
    }
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
    ShopConfig draft = existing_ ? *existing_ : ShopConfig{};
    draft.name = name_->value();
    draft.url = url_->value();
    draft.username = user_->value();
    draft.password = password_->value();
    // Deduced from the address, unless editing a source whose URL keeps the
    // same kind (its own verifyTls choice is then kept) or the demo shop.
    const bool sameKind = existing_ && (existing_->type == "mock" || SourceCatalog::typeForUrl(draft.url) == existing_->type);
    if (!sameKind) draft.type.clear();
    Status saved = existing_ ? catalog_.update(existing_->name, draft) : catalog_.add(draft);
    if (!saved) {
        brls::Logger::error("Sources: {}", saved.error().describe());
        showError(brls::getStr("retromanager/sources/error", saved.error().message));
        return;
    }
    brls::Logger::info("Sources: {} {} ({})", existing_ ? "updated" : "added", draft.name, draft.url);
    std::string name = draft.name.substr(draft.name.find_first_not_of(" \t"));
    name = name.substr(0, name.find_last_not_of(" \t") + 1);
    auto done = onSaved_;
    brls::sync([done, name] {
        brls::Application::popActivity(brls::TransitionAnimation::FADE, [done, name] {
            if (done) done(name);
        });
    });
}

}  // namespace rm::ui
