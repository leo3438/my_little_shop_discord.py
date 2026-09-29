#pragma once

#include <string>
#include <vector>

namespace rm {

// Well-known virtual paths on the SD card.
//
// These are the defaults of RetroArch for Switch, Atmosphère and sys-clk.
// RetroArch lets users relocate most of them in retroarch.cfg: services
// must treat these values as fallbacks and prefer what the configuration
// parser reads (Phase 2+).
struct SdLayout {
    // RetroArch
    std::string retroarchDir = "/retroarch";
    std::string retroarchCfg = "/retroarch/retroarch.cfg";
    std::string coresDir = "/retroarch/cores";
    std::string coreConfigDir = "/retroarch/config";  // per-core / per-game overrides
    std::string systemDir = "/retroarch/system";      // BIOS
    std::string savesDir = "/retroarch/saves";
    std::string statesDir = "/retroarch/states";
    std::string cheatsDir = "/retroarch/cheats";
    std::string thumbnailsDir = "/retroarch/thumbnails";
    std::string playlistsDir = "/retroarch/playlists";

    // Content
    std::string romsDir = "/roms";

    // System tools
    std::string sysClkConfig = "/config/sys-clk/config.ini";

    // RetroManager's own data
    std::string appDataDir = "/switch/RetroManager";
    std::string appConfig = "/switch/RetroManager/config.json";
    std::string cacheDir = "/switch/RetroManager/cache";
    std::string logsDir = "/switch/RetroManager/logs";

    // Forwarders (Phase 10): console keys dumped by Lockpick_RCM, the
    // forwarder stub the user provides, and where the generated NSPs go.
    std::string prodKeys = "/switch/prod.keys";
    std::string forwarderStubDir = "/switch/RetroManager/stub";
    std::string nspDir = "/nsp";

    // Directories RetroManager owns and creates at startup. RetroArch's own
    // directories are never created by us: their absence means RetroArch is
    // not installed, which the UI must report rather than hide.
    std::vector<std::string> appDirectories() const { return {appDataDir, cacheDir, logsDir}; }
};

}  // namespace rm
