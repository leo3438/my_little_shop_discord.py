#include "retromanager/ui/DownloadNotifier.hpp"

#include <borealis.hpp>

namespace rm::ui {

DownloadNotifier::DownloadNotifier(EventBus& bus) {
    finished_ = bus.subscribe<DownloadFinished>([](const DownloadFinished& event) {
        const std::string name = event.destination.substr(event.destination.rfind('/') + 1);
        if (event.result.ok()) {
            brls::Logger::info("Installed {}", event.destination);
            brls::Application::notify(brls::getStr("retromanager/downloads/notify_done", name));
        } else if (event.result.error().code != ErrorCode::Cancelled) {
            brls::Logger::error("Download failed: {}: {}", event.itemId, event.result.error().describe());
            brls::Application::notify(brls::getStr("retromanager/downloads/notify_failed", name));
        }
    });
}

}  // namespace rm::ui
