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
#include "retromanager/services/BiosManager.hpp"
#include "retromanager/services/CheatManager.hpp"
#include "retromanager/services/CloudSyncService.hpp"
#include "retromanager/services/ConfigManager.hpp"
#include "retromanager/services/DownloadService.hpp"
#include "retromanager/services/EmulatorConfigurator.hpp"
#include "retromanager/services/PlaylistManager.hpp"
#include "retromanager/services/ShopService.hpp"
#include "retromanager/services/SysClkConfigurator.hpp"
#include "retromanager/services/ThumbnailManager.hpp"
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

// Everything built from config.json. Problems never prevent the app from
// starting: they surface in the shop / sync screens instead.
struct RemoteSources {
    std::unique_ptr<rm::IRemoteSource> shop;
    std::unique_ptr<rm::IRemoteSource> saves;
    std::string savesBaseUrl;  // empty = cloud saves not configured
    rm::SysClkSettings sysclk;
};

RemoteSources createRemoteSources(rm::AppContext& context) {
    RemoteSources sources;
    rm::ConfigManager configManager(context.fileSystem(), context.layout().appConfig);
    auto config = configManager.loadOrCreate();
    if (!config) {
        brls::Logger::error("Configuration: {}", config.error().describe());
        rm::Error error = rm::makeError(rm::ErrorCode::NotConfigured, config.error().message);
        sources.shop = std::make_unique<rm::UnavailableRemoteSource>(error, configManager.path());
        sources.saves = std::make_unique<rm::UnavailableRemoteSource>(error, configManager.path());
        return sources;
    }

#ifdef RM_WITH_CURL
    sources.shop = rm::createRemoteSource(config.value().shop);
    rm::SavesSource saves = rm::createSavesSource(config.value());
    sources.saves = std::move(saves.source);
    sources.savesBaseUrl = std::move(saves.baseUrl);
#else
    rm::Error noCurl = rm::makeError(rm::ErrorCode::Unsupported, "built without libcurl (RM_WITH_CURL=OFF)");
    if (config.value().shop.type == "mock") {
        sources.shop = rm::MockRemoteSource::createDemo();
        auto demoNas = std::make_unique<rm::MockRemoteSource>("{}", "");
        demoNas->addDirectory("ftp://mock.local/Saves/");
        sources.saves = std::move(demoNas);
        sources.savesBaseUrl = "ftp://mock.local/Saves/";
    } else {
        sources.shop = std::make_unique<rm::UnavailableRemoteSource>(noCurl, "ftp");
        sources.saves = std::make_unique<rm::UnavailableRemoteSource>(noCurl, "ftp");
    }
#endif
    sources.sysclk = config.value().sysclk;
    if (auto* mock = dynamic_cast<rm::MockRemoteSource*>(sources.shop.get())) configureMockFromEnvironment(*mock);
    brls::Logger::info("Shop source: {}", sources.shop->describe());
    brls::Logger::info("Cloud saves: {}", sources.savesBaseUrl.empty() ? "(not configured)" : sources.savesBaseUrl);
    return sources;
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

    // The console language on Switch. On desktop, Borealis only maps a few
    // LANG values: RETROMANAGER_LANG=fr forces one.
    const char* forcedLocale = std::getenv("RETROMANAGER_LANG");
    brls::Platform::APP_LOCALE_DEFAULT = forcedLocale ? forcedLocale : brls::LOCALE_AUTO;
    if (!brls::Application::init()) {
        brls::Logger::error("Unable to init Borealis application");
        return EXIT_FAILURE;
    }

    brls::Application::createWindow("retromanager/app/title"_i18n);
    brls::Application::getPlatform()->setThemeVariant(brls::ThemeVariant::DARK);
    brls::Application::setGlobalQuit(true);  // + quits from any screen

    // Declaration order matters: destroyed in reverse, the services with a
    // worker (BIOS, sync, downloads) go first (cancel and join) while everything
    // they use still lives.
    RemoteSources remotes = createRemoteSources(context);
    rm::ui::BorealisTaskRunner uiTasks;
    rm::EventBus bus(uiTasks);
    auto onMainThread = [](std::function<void()> task) { brls::sync(task); };
    rm::RomStore romStore(context.fileSystem(), context.layout());
    rm::ShopService shop(*remotes.shop, uiTasks, &romStore);
    rm::EmulatorConfigurator emulatorConfigurator(context.fileSystem(), context.layout());
    rm::PlaylistManager playlists(context.fileSystem(), context.layout());
    rm::ThumbnailManager thumbnails(context.fileSystem(), context.layout(), *remotes.shop);
    rm::CheatManager cheatManager(context.fileSystem(), context.layout(), *remotes.shop);
    rm::SysClkConfigurator sysClk(context.fileSystem(), context.layout(), remotes.sysclk.titleId);
    rm::DownloadService downloads(*remotes.shop, romStore, bus, *platform.system,
                                  std::make_unique<rm::WorkerThread>(onMainThread));
    downloads.addPostInstallStep(emulatorConfigurator);  // after the ROM: point RetroArch's browser at it
    downloads.addPostInstallStep(playlists);             // list it in its system's playlist
    downloads.addPostInstallStep(thumbnails);            // with its box art, when the shop has one
    downloads.addPostInstallStep(cheatManager);          // then its cheats, when the shop has some
    if (remotes.sysclk.enabled) downloads.addPostInstallStep(sysClk);  // N64/PS1/3DS: full CPU speed
    rm::CloudSyncService cloudSync(context.fileSystem(), context.layout(), *remotes.saves, remotes.savesBaseUrl, bus,
                                   *platform.system, std::make_unique<rm::WorkerThread>(onMainThread));
    rm::BiosManager bios(context.fileSystem(), context.layout(), *remotes.shop, bus,
                         std::make_unique<rm::WorkerThread>(onMainThread));

    brls::Application::pushActivity(new rm::ui::HomeActivity(context, initStatus, shop, downloads, cloudSync, bios, bus));

    while (brls::Application::mainLoop()) {
    }
    return EXIT_SUCCESS;
}
