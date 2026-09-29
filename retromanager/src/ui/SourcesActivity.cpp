#include "retromanager/ui/SourcesActivity.hpp"

#include "retromanager/core/Format.hpp"
#include "retromanager/parsers/RepoIndexParser.hpp"
#include "retromanager/ui/SourceFormActivity.hpp"

namespace rm::ui {

namespace {

std::string typeLabel(const std::string& type) {
    if (type == "http") return brls::getStr("retromanager/sources/type_http");
    if (type == "mock") return brls::getStr("retromanager/sources/type_mock");
    if (type == "smb") return brls::getStr("retromanager/sources/type_smb");
    return brls::getStr("retromanager/sources/type_ftp");
}

brls::Button* smallButton(const std::string& text, std::function<void()> action) {
    auto* button = new brls::Button();
    button->setStyle(&brls::BUTTONSTYLE_BORDERED);
    button->setText(text);
    button->setWidth(150);
    button->setHeight(44);
    button->setMarginLeft(10);
    button->setFontSize(17);
    button->registerClickAction([action](brls::View*) {
        action();
        return true;
    });
    return button;
}

}  // namespace

SourcesActivity::SourcesActivity(SourceCatalog& catalog, SourceRouter& router, EventBus& bus, std::string configNotes)
    : catalog_(catalog), router_(router), bus_(bus), configNotes_(std::move(configNotes)) {}

SourcesActivity::~SourcesActivity() { *alive_ = false; }

void SourcesActivity::onContentAvailable() {
    hint->setText(configNotes_.empty() ? brls::getStr("retromanager/sources/hint")
                                       : brls::getStr("retromanager/sources/hint") + "\n\n" + configNotes_);
    root->registerAction(brls::getStr("retromanager/sources/add"), brls::BUTTON_X, [this](brls::View*) {
        openForm("");
        return true;
    });
    rebuild();
}

void SourcesActivity::rebuild(const std::string& focusName) {
    // The add button survives the rebuild: park the focus there first.
    list->clearViews();
    auto* add = new brls::Button();
    add->setStyle(&brls::BUTTONSTYLE_PRIMARY);
    add->setText(brls::getStr("retromanager/sources/add_button"));
    add->setWidth(360);
    add->setMarginBottom(16);
    add->registerClickAction([this](brls::View*) {
        openForm("");
        return true;
    });
    list->addView(add);

    brls::View* focus = nullptr;
    for (const SourceInfo& source : router_.sources()) {
        // ROW box, not focusable: a column of labels, then the buttons.
        auto* row = new brls::Box(brls::Axis::ROW);
        row->setAlignItems(brls::AlignItems::CENTER);
        row->setPadding(12, 8, 12, 16);
        row->setMarginBottom(8);
        row->setCornerRadius(6);
        row->setBackgroundColor(brls::Application::getTheme().getColor("brls/backdrop"));

        auto* labels = new brls::Box(brls::Axis::COLUMN);
        labels->setGrow(1.0f);
        labels->setShrink(1.0f);
        auto* title = new brls::Label();
        title->setFontSize(22);
        title->setText(source.active ? source.name + "  ·  " + brls::getStr("retromanager/sources/active") : source.name);
        if (source.active) title->setTextColor(brls::Application::getTheme().getColor("brls/accent"));
        labels->addView(title);
        auto* detail = new brls::Label();
        detail->setFontSize(16);
        detail->setMarginTop(4);
        detail->setText(typeLabel(source.type) + "\n" + breakLongLines(source.description, 52));
        detail->setAutoAnimate(false);
        detail->setTextColor(brls::Application::getTheme().getColor("brls/text_disabled"));
        labels->addView(detail);
        row->addView(labels);

        const std::string name = source.name;
        brls::Button* use = smallButton(brls::getStr("retromanager/sources/use"), [this, name] {
            changed(catalog_.activate(name), brls::getStr("retromanager/sources/activated", name), name);
        });
        row->addView(use);
        row->addView(smallButton(brls::getStr("retromanager/sources/edit"), [this, name] { openForm(name); }));
        row->addView(smallButton(brls::getStr("retromanager/sources/test"), [this, name] { testConnection(name); }));
        row->addView(smallButton(brls::getStr("retromanager/sources/delete"), [this, name] { confirmDelete(name); }));
        // Y anywhere on the row: edit this source.
        row->registerAction(brls::getStr("retromanager/sources/edit"), brls::BUTTON_Y, [this, name](brls::View*) {
            openForm(name);
            return true;
        });
        list->addView(row);
        if (focus == nullptr && (focusName.empty() ? source.active : source.name == focusName)) focus = use;
    }
    if (router_.sources().empty()) {
        auto* empty = new brls::Label();
        empty->setText(brls::getStr("retromanager/sources/empty"));
        empty->setMarginTop(8);
        list->addView(empty);
    }
    brls::Application::giveFocus(focus != nullptr ? focus : add);
}

void SourcesActivity::openForm(const std::string& editName) {
    std::optional<ShopConfig> existing;
    if (!editName.empty()) {
        for (const ShopConfig& source : catalog_.config().sources) {
            if (source.name == editName) existing = source;
        }
        if (!existing) return;
    }
    const bool editing = existing.has_value();
    brls::Application::pushActivity(new SourceFormActivity(
        catalog_,
        [this, editing](const std::string& name) {
            brls::Application::notify(brls::getStr(editing ? "retromanager/sources/updated" : "retromanager/sources/added", name));
            rebuild(name);
            bus_.publish(SourcesChanged{});
        },
        existing));
}

void SourcesActivity::confirmDelete(const std::string& name) {
    auto* dialog = new brls::Dialog(brls::getStr("retromanager/sources/confirm_delete", name));
    dialog->addButton(brls::getStr("retromanager/sources/cancel"), [] {});
    dialog->addButton(brls::getStr("retromanager/sources/delete"), [this, name] {
        brls::sync([this, name] { changed(catalog_.remove(name), brls::getStr("retromanager/sources/deleted", name)); });
    });
    dialog->open();
}

void SourcesActivity::testConnection(const std::string& name) {
    std::shared_ptr<IRemoteSource> source = router_.sourceNamed(name);
    if (!source) return;
    brls::Application::notify(brls::getStr("retromanager/sources/testing", name));
    brls::Logger::info("Sources: testing {} ({})", name, source->describe());
    std::weak_ptr<bool> alive = alive_;
    brls::async([source, name, alive] {
        Result<std::string> index = source->fetchIndex();
        std::string text;
        if (!index) {
            brls::Logger::error("Sources: test of {} failed: {}", name, index.error().describe());
            text = brls::getStr("retromanager/sources/test_failed", name, index.error().message);
        } else {
            auto parsed = RepoIndexParser(source->indexUrl()).parse(index.value());
            if (!parsed) {
                brls::Logger::error("Sources: {} answered but its index is invalid: {}", name, parsed.error().describe());
                text = brls::getStr("retromanager/sources/test_bad_index", name, parsed.error().message);
            } else {
                brls::Logger::info("Sources: test of {} OK", name);
                text = brls::getStr("retromanager/sources/test_ok", name, std::to_string(parsed.value().games.size()),
                                    std::to_string(parsed.value().apps.size()), formatBytes(index.value().size()));
            }
        }
        brls::sync([alive, text] {
            auto flag = alive.lock();
            if (!flag || !*flag) return;
            auto* dialog = new brls::Dialog(text);
            dialog->addButton(brls::getStr("retromanager/download/close"), [] {});
            dialog->open();
        });
    });
}

void SourcesActivity::changed(const Status& result, const std::string& okMessage, const std::string& focusName) {
    if (!result) {
        brls::Logger::error("Sources: {}", result.error().describe());
        brls::Application::notify(brls::getStr("retromanager/sources/error", result.error().message));
        return;
    }
    brls::Application::notify(okMessage);
    rebuild(focusName);
    bus_.publish(SourcesChanged{});
}

}  // namespace rm::ui
