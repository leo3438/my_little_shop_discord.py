#include "retromanager/parsers/ConfigParser.hpp"

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
    return root.dump(2) + "\n";
}

}  // namespace rm
