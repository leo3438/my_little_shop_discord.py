#include "retromanager/parsers/ConfigParser.hpp"

#include <algorithm>
#include <cctype>
#include <nlohmann/json.hpp>

namespace rm {

namespace {

using json = nlohmann::json;

Status readString(const json& object, const char* key, std::string& out) {
    auto it = object.find(key);
    if (it == object.end() || it->is_null()) return success();
    if (!it->is_string()) return makeError(ErrorCode::ParseError, std::string("\"shop.") + key + "\" must be a string");
    out = it->get<std::string>();
    return success();
}

}  // namespace

Result<AppConfig> parseConfig(std::string_view document) {
    json root;
    try {
        root = json::parse(document.begin(), document.end());
    } catch (const json::parse_error& e) {
        return makeError(ErrorCode::ParseError, e.what());
    }
    if (!root.is_object()) return makeError(ErrorCode::ParseError, "configuration root must be a JSON object");

    if (auto it = root.find("version"); it != root.end()) {
        if (!it->is_number_integer()) return makeError(ErrorCode::ParseError, "\"version\" must be an integer");
        if (it->get<std::int64_t>() > AppConfig::kVersion) {
            return makeError(ErrorCode::Unsupported, "configuration written by a newer RetroManager");
        }
    }

    AppConfig config;

    if (auto it = root.find("saves_url"); it != root.end() && !it->is_null()) {
        if (!it->is_string()) return makeError(ErrorCode::ParseError, "\"saves_url\" must be a string");
        config.savesUrl = it->get<std::string>();
    }
    if (auto sysclk = root.find("sysclk"); sysclk != root.end() && !sysclk->is_null()) {
        if (!sysclk->is_object()) return makeError(ErrorCode::ParseError, "\"sysclk\" must be an object");
        if (auto it = sysclk->find("enabled"); it != sysclk->end() && !it->is_null()) {
            if (!it->is_boolean()) return makeError(ErrorCode::ParseError, "\"sysclk.enabled\" must be true or false");
            config.sysclk.enabled = it->get<bool>();
        }
        if (auto it = sysclk->find("title_id"); it != sysclk->end() && !it->is_null()) {
            if (!it->is_string()) return makeError(ErrorCode::ParseError, "\"sysclk.title_id\" must be a string");
            config.sysclk.titleId = it->get<std::string>();
            bool hex16 = config.sysclk.titleId.size() == 16 &&
                         std::all_of(config.sysclk.titleId.begin(), config.sysclk.titleId.end(),
                                     [](unsigned char c) { return std::isxdigit(c) != 0; });
            if (!hex16) return makeError(ErrorCode::ParseError, "\"sysclk.title_id\" must be 16 hexadecimal digits");
        }
    }

    auto shop = root.find("shop");
    if (shop == root.end() || shop->is_null()) return config;
    if (!shop->is_object()) return makeError(ErrorCode::ParseError, "\"shop\" must be an object");

    for (auto [key, target] : {std::pair<const char*, std::string*>{"type", &config.shop.type},
                               {"url", &config.shop.url},
                               {"username", &config.shop.username},
                               {"password", &config.shop.password}}) {
        if (Status s = readString(*shop, key, *target); !s) return s.error();
    }
    if (config.shop.type != "ftp" && config.shop.type != "mock") {
        return makeError(ErrorCode::ParseError, "\"shop.type\" must be \"ftp\" or \"mock\", not \"" + config.shop.type + "\"");
    }
    if (auto it = shop->find("verifyTls"); it != shop->end() && !it->is_null()) {
        if (!it->is_boolean()) return makeError(ErrorCode::ParseError, "\"shop.verifyTls\" must be true or false");
        config.shop.verifyTls = it->get<bool>();
    }
    return config;
}

std::string serializeConfig(const AppConfig& config) {
    // ordered_json keeps the documented key order for humans editing the file.
    nlohmann::ordered_json root;
    root["version"] = AppConfig::kVersion;
    root["shop"]["type"] = config.shop.type;
    root["shop"]["url"] = config.shop.url;
    root["shop"]["username"] = config.shop.username;
    root["shop"]["password"] = config.shop.password;
    root["shop"]["verifyTls"] = config.shop.verifyTls;
    root["saves_url"] = config.savesUrl;
    root["sysclk"]["enabled"] = config.sysclk.enabled;
    root["sysclk"]["title_id"] = config.sysclk.titleId;
    return root.dump(2) + "\n";
}

}  // namespace rm
