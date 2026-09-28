#include <borealis.hpp>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>

#include "retromanager/core/AppContext.hpp"
#include "retromanager/core/EventBus.hpp"
#include "retromanager/core/WorkerThread.hpp"
#include "retromanager/fs/RomStore.hpp"
#include "retromanager/network/MockRemoteSource.hpp"
#include "retromanager/platform/Platform.hpp"
#include "retromanager/services/CheatManager.hpp"
#include "retromanager/services/ConfigManager.hpp"
#include "retromanager/services/DownloadService.hpp"
#include "retromanager/services/EmulatorConfigurator.hpp"
#include "retromanager/services/ShopService.hpp"
#include "retromanager/ui/BorealisTaskRunner.hpp"
#include "retromanager/ui/HomeActivity.hpp"

#ifdef RM_WITH_CURL
#include "retromanager/network/SourceFactory.hpp"
#endif

using namespace brls::literals;  // _i18n

namespace {

// Debug knobs for the demo shop ("type": "mock" in config.json), to
// exercise the loading, progress and error screens:
//   RETROMANAGER_MOCK_LATENCY_MS=3000
//   RETROMANAGER_MOCK_ERROR=network|auth|notfound|format
//   RETROMANAGER_MOCK_SPEED_KBPS=2048   (download speed, default 16384)
void configureMockFromEnvironment(rm::MockRemoteSource& source) {
    source.setThroughput(16ull * 1024 * 1024);  // make the progress bar visible
    if (const char* speed = std::getenv("RETROMANAGER_MOCK_SPEED_KBPS")) {
        source.setThroughput(std::strtoull(speed, nullptr, 10) * 1024);
    }
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

// config.json -> the shop source. Problems never prevent the app from
// starting: they surface in the shop screen instead.
std::unique_ptr<rm::IRemoteSource> createShopSource(rm::AppContext& context) {
    rm::ConfigManager configManager(context.fileSystem(), context.layout().appConfig);
    auto config = configManager.loadOrCreate();
    if (!config) {
        brls::Logger::error("Configuration: {}", config.error().describe());
        return std::make_unique<rm::UnavailableRemoteSource>(
            rm::makeError(rm::ErrorCode::NotConfigured, config.error().message), configManager.path());
    }

    std::unique_ptr<rm::IRemoteSource> source;
#ifdef RM_WITH_CURL
    source = rm::createRemoteSource(config.value().shop);
#else
    source = config.value().shop.type == "mock"
                 ? std::unique_ptr<rm::IRemoteSource>(rm::MockRemoteSource::createDemo())
                 : std::make_unique<rm::UnavailableRemoteSource>(
                       rm::makeError(rm::ErrorCode::Unsupported, "built without libcurl (RM_WITH_CURL=OFF)"), "ftp");
#endif
    if (auto* mock = dynamic_cast<rm::MockRemoteSource*>(source.get())) configureMockFromEnvironment(*mock);
    brls::Logger::info("Shop source: {}", source->describe());
    return source;
}

}  // namespace

int main(int argc, char* argv[]) {
    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "-d") == 0) brls::Logger::setLogLevel(brls::LogLevel::LOG_DEBUG);
        if (std::strcmp(argv[i], "-v") == 0) brls::Application::enableDebuggingView(true);
    }

    // Composition root: the only place that knows which platform we run on
    // and which concrete services are used.
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

    // Declaration order matters: destroyed in reverse, DownloadService goes
    // first (cancels and joins its worker) while everything it uses lives.
    std::unique_ptr<rm::IRemoteSource> shopSource = createShopSource(context);
    rm::ui::BorealisTaskRunner uiTasks;
    rm::EventBus bus(uiTasks);
    rm::RomStore romStore(context.fileSystem(), context.layout());
    rm::ShopService shop(*shopSource, uiTasks, &romStore);
    rm::EmulatorConfigurator emulatorConfigurator(context.fileSystem(), context.layout());
    rm::CheatManager cheatManager(context.fileSystem(), context.layout(), *shopSource);
    rm::DownloadService downloads(*shopSource, romStore, bus, *platform.system,
                                  std::make_unique<rm::WorkerThread>([](std::function<void()> task) { brls::sync(task); }));
    downloads.addPostInstallStep(emulatorConfigurator);  // after the ROM: point RetroArch at it
    downloads.addPostInstallStep(cheatManager);          // then its cheats, when the shop has some

    brls::Application::pushActivity(new rm::ui::HomeActivity(context, initStatus, shop, downloads, bus));

    while (brls::Application::mainLoop()) {
    }
    return EXIT_SUCCESS;
}
