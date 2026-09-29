#include "retromanager/parsers/ConfigParser.hpp"

#include <algorithm>
#include <cctype>
#include <nlohmann/json.hpp>

namespace rm {

namespace {

using json = nlohmann::json;

Status readString(const json& object, const char* key, std::string& out, const std::string& where) {
    auto it = object.find(key);
    if (it == object.end() || it->is_null()) return success();
    if (!it->is_string()) return makeError(ErrorCode::ParseError, "\"" + where + key + "\" must be a string");
    out = it->get<std::string>();
    return success();
}

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

Result<ShopConfig> parseSource(const json& object, const std::string& where, const std::string& defaultName) {
    if (!object.is_object()) return makeError(ErrorCode::ParseError, "\"" + where + "\" must be an object");
    ShopConfig source;
    source.name = defaultName;
    source.type.clear();
    const std::string prefix = where + ".";
    for (auto [key, target] : {std::pair<const char*, std::string*>{"name", &source.name},
                               {"type", &source.type},
                               {"url", &source.url},
                               {"username", &source.username},
                               {"password", &source.password}}) {
        if (Status s = readString(object, key, *target, prefix); !s) return s.error();
    }
    if (source.name.empty()) source.name = defaultName;
    if (source.type.empty()) {  // from the URL: http(s):// is a web shop, anything else a NAS
        std::string scheme = lower(source.url.substr(0, source.url.find(':')));
        source.type = scheme == "http" || scheme == "https" ? "http" : "ftp";
    }
    if (source.type != "ftp" && source.type != "http" && source.type != "mock") {
        return makeError(ErrorCode::ParseError,
                         "\"" + prefix + "type\" must be \"ftp\", \"http\" or \"mock\", not \"" + source.type + "\"");
    }
    source.verifyTls = source.type == "http";
    if (auto it = object.find("verifyTls"); it != object.end() && !it->is_null()) {
        if (!it->is_boolean()) return makeError(ErrorCode::ParseError, "\"" + prefix + "verifyTls\" must be true or false");
        source.verifyTls = it->get<bool>();
    }
    return source;
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

    if (auto it = root.find("ca_bundle"); it != root.end() && !it->is_null()) {
        if (!it->is_string()) return makeError(ErrorCode::ParseError, "\"ca_bundle\" must be a string");
        config.caBundle = it->get<std::string>();
    }
    if (auto scraper = root.find("scraper"); scraper != root.end() && !scraper->is_null()) {
        if (!scraper->is_object()) return makeError(ErrorCode::ParseError, "\"scraper\" must be an object");
        if (auto it = scraper->find("enabled"); it != scraper->end() && !it->is_null()) {
            if (!it->is_boolean()) return makeError(ErrorCode::ParseError, "\"scraper.enabled\" must be true or false");
            config.scraper.enabled = it->get<bool>();
        }
        if (Status s = readString(*scraper, "base_url", config.scraper.baseUrl, "scraper."); !s) return s.error();
    }

    // Sources: "sources" (current format), else the single "shop" of the
    // first versions, which becomes the "NAS" source.
    auto sources = root.find("sources");
    auto shop = root.find("shop");
    if (sources != root.end() && !sources->is_null()) {
        if (!sources->is_array()) return makeError(ErrorCode::ParseError, "\"sources\" must be an array");
        config.sources.clear();
        for (std::size_t i = 0; i < sources->size(); ++i) {
            auto source = parseSource((*sources)[i], "sources[" + std::to_string(i) + "]", "Source " + std::to_string(i + 1));
            if (!source) return source.error();
            for (const ShopConfig& other : config.sources) {
                if (lower(other.name) == lower(source.value().name)) {
                    return makeError(ErrorCode::ParseError, "two sources are named \"" + source.value().name + "\"");
                }
            }
            config.sources.push_back(std::move(source.value()));
        }
    } else if (shop != root.end() && !shop->is_null()) {
        auto source = parseSource(*shop, "shop", "NAS");
        if (!source) return source.error();
        config.sources = {std::move(source.value())};
    }

    std::string active = config.sources.empty() ? "" : config.sources.front().name;
    if (auto it = root.find("active_source"); it != root.end() && !it->is_null()) {
        if (!it->is_string()) return makeError(ErrorCode::ParseError, "\"active_source\" must be a string");
        for (const ShopConfig& source : config.sources) {
            if (lower(source.name) == lower(it->get<std::string>())) active = source.name;  // deleted: the first one
        }
    }
    config.activeSource = active;
    return config;
}

std::string serializeConfig(const AppConfig& config) {
    // ordered_json keeps the documented key order for humans editing the file.
    nlohmann::ordered_json root;
    root["version"] = AppConfig::kVersion;
    root["sources"] = nlohmann::ordered_json::array();
    for (const ShopConfig& source : config.sources) {
        nlohmann::ordered_json entry;
        entry["name"] = source.name;
        entry["type"] = source.type;
        entry["url"] = source.url;
        entry["username"] = source.username;
        entry["password"] = source.password;
        entry["verifyTls"] = source.verifyTls;
        root["sources"].push_back(std::move(entry));
    }
    root["active_source"] = config.activeSource;
    root["saves_url"] = config.savesUrl;
    root["sysclk"]["enabled"] = config.sysclk.enabled;
    root["sysclk"]["title_id"] = config.sysclk.titleId;
    root["scraper"]["enabled"] = config.scraper.enabled;
    root["scraper"]["base_url"] = config.scraper.baseUrl;
    root["ca_bundle"] = config.caBundle;
    return root.dump(2) + "\n";
}

}  // namespace rm
