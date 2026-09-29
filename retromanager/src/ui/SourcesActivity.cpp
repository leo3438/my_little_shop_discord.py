#include "retromanager/ui/SourcesActivity.hpp"

namespace rm::ui {

namespace {

std::string trim(const std::string& text) {
    std::size_t begin = text.find_first_not_of(" \t");
    std::size_t end = text.find_last_not_of(" \t");
    return begin == std::string::npos ? "" : text.substr(begin, end - begin + 1);
}

std::string typeLabel(const std::string& type) {
    if (type == "http") return brls::getStr("retromanager/sources/type_http");
    if (type == "mock") return brls::getStr("retromanager/sources/type_mock");
    return brls::getStr("retromanager/sources/type_ftp");
}

// Opens the next keyboard once the current one is closed.
void nextPrompt(std::function<void()> open) { brls::sync(std::move(open)); }

}  // namespace

SourcesActivity::SourcesActivity(SourceCatalog& catalog, SourceRouter& router, EventBus& bus)
    : catalog_(catalog), router_(router), bus_(bus) {}

void SourcesActivity::onContentAvailable() {
    hint->setText(brls::getStr("retromanager/sources/hint"));
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
        auto* cell = new brls::DetailCell();
        cell->setText(source.active ? source.name + "  ·  " + brls::getStr("retromanager/sources/active") : source.name);
        cell->setDetailText(typeLabel(source.type) + "  ·  " + source.description);
        cell->setDetailTextColor(brls::Application::getTheme().getColor("brls/text_disabled"));
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
    auto draft = std::make_shared<ShopConfig>();
    draft->type.clear();  // deduced from the address
    auto* ime = brls::Application::getImeManager();
    ime->openForText(
        [this, draft](std::string name) {
            draft->name = trim(name);
            if (draft->name.empty()) return;
            nextPrompt([this, draft] {
                brls::Application::getImeManager()->openForText(
                    [this, draft](std::string address) {
                        draft->url = trim(address);
                        if (draft->url.empty()) return;
                        nextPrompt([this, draft] {
                            brls::Application::getImeManager()->openForText(
                                [this, draft](std::string user) {
                                    draft->username = trim(user);
                                    if (draft->username.empty()) return finishAdd(draft);  // anonymous
                                    nextPrompt([this, draft] {
                                        brls::Application::getImeManager()->openForText(
                                            [this, draft](std::string password) {
                                                draft->password = password;
                                                finishAdd(draft);
                                            },
                                            brls::getStr("retromanager/sources/prompt_password"), "", 128);
                                    });
                                },
                                brls::getStr("retromanager/sources/prompt_user"),
                                brls::getStr("retromanager/sources/prompt_user_hint"), 64);
                        });
                    },
                    brls::getStr("retromanager/sources/prompt_url"), brls::getStr("retromanager/sources/prompt_url_hint"),
                    256);
            });
        },
        brls::getStr("retromanager/sources/prompt_name"), "", 32);
}

void SourcesActivity::finishAdd(const std::shared_ptr<ShopConfig>& draft) {
    brls::sync([this, draft] {
        brls::Logger::info("Sources: adding {} ({})", draft->name, draft->url);
        changed(catalog_.add(*draft), brls::getStr("retromanager/sources/added", draft->name));
    });
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
