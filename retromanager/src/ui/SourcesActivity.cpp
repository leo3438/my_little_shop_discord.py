#include "retromanager/ui/SourcesActivity.hpp"

#include "retromanager/core/Format.hpp"
#include "retromanager/ui/SourceFormActivity.hpp"

namespace rm::ui {

namespace {

std::string typeLabel(const std::string& type) {
    if (type == "http") return brls::getStr("retromanager/sources/type_http");
    if (type == "mock") return brls::getStr("retromanager/sources/type_mock");
    return brls::getStr("retromanager/sources/type_ftp");
}

}  // namespace

SourcesActivity::SourcesActivity(SourceCatalog& catalog, SourceRouter& router, EventBus& bus, std::string configNotes)
    : catalog_(catalog), router_(router), bus_(bus), configNotes_(std::move(configNotes)) {}

void SourcesActivity::onContentAvailable() {
    hint->setText(configNotes_.empty() ? brls::getStr("retromanager/sources/hint")
                                       : brls::getStr("retromanager/sources/hint") + "\n\n" + configNotes_);
    root->registerAction(brls::getStr("retromanager/sources/add"), brls::BUTTON_X, [this](brls::View*) {
        promptAdd();
        return true;
    });
    rebuild();
}

void SourcesActivity::rebuild() {
    brls::Application::giveFocus(hint);  // never keep the focus on a row about to be deleted
    list->clearViews();
    brls::View* first = nullptr;
    brls::View* active = nullptr;
    for (const SourceInfo& source : router_.sources()) {
        // Name, then type and address on their own line: a long URL wraps
        // instead of being cut off at the right edge.
        auto* cell = new brls::Box(brls::Axis::COLUMN);
        cell->setFocusable(true);
        cell->setPadding(12, 16, 12, 16);
        cell->setMarginBottom(6);
        auto* title = new brls::Label();
        title->setFontSize(22);
        title->setText(source.active ? source.name + "  ·  " + brls::getStr("retromanager/sources/active") : source.name);
        if (source.active) title->setTextColor(brls::Application::getTheme().getColor("brls/accent"));
        cell->addView(title);
        auto* detail = new brls::Label();
        detail->setFontSize(17);
        detail->setMarginTop(4);
        detail->setText(breakLongLines(typeLabel(source.type) + "  ·  " + source.description, 90));
        detail->setWidthPercentage(100);
        detail->setAutoAnimate(false);
        detail->setTextColor(brls::Application::getTheme().getColor("brls/text_disabled"));
        cell->addView(detail);
        cell->addGestureRecognizer(new brls::TapGestureRecognizer(cell));
        const std::string name = source.name;
        cell->registerClickAction([this, name](brls::View*) {
            openActions(name);
            return true;
        });
        list->addView(cell);
        if (first == nullptr) first = cell;
        if (source.active) active = cell;
    }
    if (first == nullptr) {
        auto* empty = new brls::Label();
        empty->setText(brls::getStr("retromanager/sources/empty"));
        empty->setFocusable(true);
        list->addView(empty);
        first = empty;
    }
    brls::Application::giveFocus(active != nullptr ? active : first);
}

void SourcesActivity::openActions(const std::string& name) {
    auto* dialog = new brls::Dialog(name);
    dialog->addButton(brls::getStr("retromanager/sources/use"), [this, name] {
        brls::sync([this, name] { changed(catalog_.activate(name), brls::getStr("retromanager/sources/activated", name)); });
    });
    dialog->addButton(brls::getStr("retromanager/sources/delete"), [this, name] {
        brls::sync([this, name] { changed(catalog_.remove(name), brls::getStr("retromanager/sources/deleted", name)); });
    });
    dialog->addButton(brls::getStr("retromanager/download/close"), [] {});
    dialog->open();
}

void SourcesActivity::promptAdd() {
    brls::Application::pushActivity(new SourceFormActivity(catalog_, [this](const std::string& name) {
        brls::Application::notify(brls::getStr("retromanager/sources/added", name));
        rebuild();
        bus_.publish(SourcesChanged{});
    }));
}

void SourcesActivity::changed(const Status& result, const std::string& okMessage) {
    if (!result) {
        brls::Logger::error("Sources: {}", result.error().describe());
        brls::Application::notify(brls::getStr("retromanager/sources/error", result.error().message));
        return;
    }
    brls::Application::notify(okMessage);
    rebuild();
    bus_.publish(SourcesChanged{});
}

}  // namespace rm::ui
