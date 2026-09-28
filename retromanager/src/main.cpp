#include <borealis.hpp>
#include <cstdlib>
#include <chrono>
#include <cstring>
#include <string>

#include "retromanager/core/AppContext.hpp"
#include "retromanager/network/MockRemoteSource.hpp"
#include "retromanager/platform/Platform.hpp"
#include "retromanager/services/ShopService.hpp"
#include "retromanager/ui/BorealisTaskRunner.hpp"
#include "retromanager/ui/HomeActivity.hpp"

using namespace brls::literals;  // _i18n

namespace {

// Debug knobs to exercise the shop's loading and error screens:
//   RETROMANAGER_MOCK_LATENCY_MS=3000
//   RETROMANAGER_MOCK_ERROR=network|auth|notfound|format
void configureMockFromEnvironment(rm::MockRemoteSource& source) {
    if (const char* latency = std::getenv("RETROMANAGER_MOCK_LATENCY_MS")) {
        source.setLatency(std::chrono::milliseconds(std::atol(latency)));
    }
    if (const char* error = std::getenv("RETROMANAGER_MOCK_ERROR")) {
        std::string kind(error);
        rm::ErrorCode code = kind == "auth"       ? rm::ErrorCode::AuthenticationFailed
                             : kind == "notfound" ? rm::ErrorCode::NotFound
                             : kind == "format"   ? rm::ErrorCode::ParseError
                                                  : rm::ErrorCode::NetworkError;
        source.setFailure(rm::makeError(code, "simulated by RETROMANAGER_MOCK_ERROR"));
    }
}

}  // namespace

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

    // Phase 2: the shop is served by the in-memory mock. The FtpClient will
    // be selected here once sources are configurable (config.json).
    rm::ui::BorealisTaskRunner tasks;
    rm::MockRemoteSource shopSource;
    configureMockFromEnvironment(shopSource);
    rm::ShopService shop(shopSource, tasks);

    brls::Application::pushActivity(new rm::ui::HomeActivity(context, initStatus, shop));

    while (brls::Application::mainLoop()) {
    }
    return EXIT_SUCCESS;
}
