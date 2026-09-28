#include <borealis.hpp>
#include <cstdlib>
#include <cstring>

#include "retromanager/core/AppContext.hpp"
#include "retromanager/platform/Platform.hpp"
#include "retromanager/ui/HomeActivity.hpp"

using namespace brls::literals;  // _i18n

int main(int argc, char* argv[]) {
    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "-d") == 0) brls::Logger::setLogLevel(brls::LogLevel::LOG_DEBUG);
        if (std::strcmp(argv[i], "-v") == 0) brls::Application::enableDebuggingView(true);
    }

    // Composition root: the only place that knows which platform we run on.
    rm::PlatformServices platform = rm::createPlatformServices();
    rm::AppContext context(platform.fileSystem, rm::SdLayout{}, platform.name, platform.sdRootLabel);
    rm::Status initStatus = context.initialize();
    if (!initStatus) brls::Logger::error("RetroManager init failed: {}", initStatus.error().describe());

    brls::Platform::APP_LOCALE_DEFAULT = brls::LOCALE_AUTO;
    if (!brls::Application::init()) {
        brls::Logger::error("Unable to init Borealis application");
        return EXIT_FAILURE;
    }

    brls::Application::createWindow("retromanager/app/title"_i18n);
    brls::Application::getPlatform()->setThemeVariant(brls::ThemeVariant::DARK);
    brls::Application::setGlobalQuit(true);  // + quits from any screen

    brls::Application::pushActivity(new rm::ui::HomeActivity(context, initStatus));

    while (brls::Application::mainLoop()) {
    }
    return EXIT_SUCCESS;
}
