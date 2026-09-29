#pragma once

#include <string>
#include <vector>

namespace rm {

// One shop (a "source"). Stored in /switch/RetroManager/config.json.
struct ShopConfig {
    std::string name = "NAS";  // unique (ignoring case), shown in the Sources screen
    // "ftp": a NAS (FtpClient). "http": a web shop (HttpClient).
    // "mock": the built-in demo shop.
    std::string type = "ftp";
    // Index location, e.g. "ftp://nas.local:21/shop/index.json" or
    // "https://example.org/retro/shop.json". "ftps://" = explicit FTPS
    // (AUTH TLS). A URL ending with '/' means "<url>index.json". Empty = not
    // configured yet.
    std::string url;
    std::string username;
    std::string password;  // stored in clear text on the SD card (see docs/CONFIG.md)
    // FTP: off by default, home NAS use self-signed certificates the
    // console cannot validate (TLS still encrypts). HTTP: on by default, web
    // sites have public certificates (see AppConfig::caBundle).
    bool verifyTls = false;

    bool operator==(const ShopConfig& o) const {
        return name == o.name && type == o.type && url == o.url && username == o.username && password == o.password &&
               verifyTls == o.verifyTls;
    }
};

// Box art fallback when the index has none (or it cannot be fetched):
// <baseUrl><libretro system>/Named_Boxarts/<ROM name>.png.
struct ScraperSettings {
    bool enabled = true;
    std::string baseUrl = "https://thumbnails.libretro.com/";

    bool operator==(const ScraperSettings& o) const { return enabled == o.enabled && baseUrl == o.baseUrl; }
};

// sys-clk overclocking profile written after installing N64/PS1/3DS games.
struct SysClkSettings {
    bool enabled = true;
    // Title id sys-clk sees while RetroArch runs (see docs/CONFIG.md).
    std::string titleId = "010000000000100D";  // Album applet: RetroArch .nro run from hbmenu

    bool operator==(const SysClkSettings& o) const { return enabled == o.enabled && titleId == o.titleId; }
};

struct AppConfig {
    static constexpr int kVersion = 1;
    // The shops, in the user's order. The default is one empty "NAS" entry
    // to fill in.
    std::vector<ShopConfig> sources = {ShopConfig{}};
    std::string activeSource = "NAS";  // name of the source the shop screen shows
    // PEM file for HTTPS verification (host path, "sdmc:/..." on Switch);
    // empty = libcurl's default.
    std::string caBundle;
    ScraperSettings scraper;
    // Folder receiving the save files, e.g. "ftp://nas.local/Saves/". Empty =
    // cloud saves disabled. Uses the shop credentials when on the same server.
    std::string savesUrl;
    SysClkSettings sysclk;
    // What the parser skipped or replaced in a hand-edited config.json
    // ("sources[2] (\"Maison\"): unknown type \"smb\""...). Not saved,
    // not compared: shown in the Sources screen and the log.
    std::vector<std::string> warnings;

    // nullptr when there is no source at all.
    const ShopConfig* active() const {
        for (const ShopConfig& source : sources) {
            if (source.name == activeSource) return &source;
        }
        return sources.empty() ? nullptr : &sources.front();
    }
    ShopConfig activeShop() const { return active() != nullptr ? *active() : ShopConfig{}; }

    bool operator==(const AppConfig& o) const {
        return sources == o.sources && activeSource == o.activeSource && caBundle == o.caBundle &&
               scraper == o.scraper && savesUrl == o.savesUrl && sysclk == o.sysclk;
    }
};

}  // namespace rm
