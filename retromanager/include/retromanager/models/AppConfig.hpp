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

struct AppConfig {
    static constexpr int kVersion = 1;
    ShopConfig shop;

    bool operator==(const AppConfig& o) const { return shop == o.shop; }
};

}  // namespace rm
