#pragma once

#include <string>

namespace rm {

// Where the shop lives. Stored in /switch/RetroManager/config.json.
struct ShopConfig {
    // "ftp": a real server (FtpClient). "mock": the built-in demo shop.
    std::string type = "ftp";
    // Index location, e.g. "ftp://nas.local:21/shop/index.json".
    // "ftps://" = explicit FTPS (AUTH TLS). A URL ending with '/' means
    // "<url>index.json". Empty = not configured yet.
    std::string url;
    std::string username;
    std::string password;  // stored in clear text on the SD card (see docs/CONFIG.md)
    // Off by default: home NAS use self-signed certificates the console
    // cannot validate (and the Switch has no CA bundle for libcurl). TLS
    // still encrypts; turn this on for a server with a recognized certificate.
    bool verifyTls = false;

    bool operator==(const ShopConfig& o) const {
        return type == o.type && url == o.url && username == o.username && password == o.password &&
               verifyTls == o.verifyTls;
    }
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
    ShopConfig shop;
    // Folder receiving the save files, e.g. "ftp://nas.local/Saves/". Empty =
    // cloud saves disabled. Uses the shop credentials when on the same server.
    std::string savesUrl;
    SysClkSettings sysclk;

    bool operator==(const AppConfig& o) const { return shop == o.shop && savesUrl == o.savesUrl && sysclk == o.sysclk; }
};

}  // namespace rm
