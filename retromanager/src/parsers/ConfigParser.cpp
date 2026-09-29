#include "retromanager/parsers/ConfigParser.hpp"

#include <algorithm>
#include <cctype>
#include <nlohmann/json.hpp>

namespace rm {

namespace {

using json = nlohmann::json;

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

// Drops what JSON does not allow but hand-written files contain: the UTF-8
// BOM, comments and trailing commas. Strings are copied untouched. Line
// breaks are kept so parse errors still point at the right line.
std::string relaxJson(std::string_view in) {
    if (in.substr(0, 3) == "\xEF\xBB\xBF") in.remove_prefix(3);
    std::string out;
    out.reserve(in.size());
    for (std::size_t i = 0; i < in.size(); ++i) {
        char c = in[i];
        if (c == '"') {
            out += c;
            for (++i; i < in.size(); ++i) {
                out += in[i];
                if (in[i] == '\\' && i + 1 < in.size()) {
                    out += in[++i];
                } else if (in[i] == '"') {
                    break;
                }
            }
        } else if (c == '/' && i + 1 < in.size() && in[i + 1] == '/') {
            while (i < in.size() && in[i] != '\n') ++i;
            if (i < in.size()) out += '\n';
        } else if (c == '/' && i + 1 < in.size() && in[i + 1] == '*') {
            for (i += 2; i < in.size() && !(in[i] == '*' && i + 1 < in.size() && in[i + 1] == '/'); ++i) {
                if (in[i] == '\n') out += '\n';
            }
            ++i;  // the closing '/'
        } else if (c == ',') {
            std::size_t next = i + 1;
            // Skip whitespace and comments to see what follows the comma.
            while (next < in.size()) {
                if (std::isspace(static_cast<unsigned char>(in[next]))) {
                    ++next;
                } else if (in[next] == '/' && next + 1 < in.size() && in[next + 1] == '/') {
                    while (next < in.size() && in[next] != '\n') ++next;
                } else if (in[next] == '/' && next + 1 < in.size() && in[next + 1] == '*') {
                    next += 2;
                    while (next + 1 < in.size() && !(in[next] == '*' && in[next + 1] == '/')) ++next;
                    next += 2;
                } else {
                    break;
                }
            }
            if (next >= in.size() || (in[next] != '}' && in[next] != ']')) out += c;  // else: trailing comma
        } else {
            out += c;
        }
    }
    return out;
}

// "line 3, column 25" for a byte offset of the relaxed document.
std::string position(const std::string& text, std::size_t byte) {
    byte = std::min(byte, text.size());
    std::size_t line = 1 + static_cast<std::size_t>(std::count(text.begin(), text.begin() + static_cast<std::ptrdiff_t>(byte), '\n'));
    std::size_t lineStart = text.rfind('\n', byte == 0 ? 0 : byte - 1);
    std::size_t column = lineStart == std::string::npos ? byte : byte - lineStart - 1;
    return "line " + std::to_string(line) + ", column " + std::to_string(column + 1);
}

// The first of `keys` present and not null.
const json* field(const json& object, std::initializer_list<const char*> keys) {
    for (const char* key : keys) {
        auto it = object.find(key);
        if (it != object.end() && !it->is_null()) return &*it;
    }
    return nullptr;
}

// A string, or a number written without quotes (passwords...).
bool asText(const json& value, std::string& out) {
    if (value.is_string()) {
        out = value.get<std::string>();
    } else if (value.is_number_integer()) {
        out = std::to_string(value.get<std::int64_t>());
    } else if (value.is_number_unsigned()) {
        out = std::to_string(value.get<std::uint64_t>());
    } else {
        return false;
    }
    return true;
}

// Strictly a string (URLs, paths, names).
bool asString(const json& value, std::string& out) {
    if (!value.is_string()) return false;
    out = value.get<std::string>();
    return true;
}

// true / false, or their spelled-out forms.
bool asBool(const json& value, bool& out) {
    if (value.is_boolean()) {
        out = value.get<bool>();
        return true;
    }
    if (!value.is_string()) return false;
    std::string text = lower(value.get<std::string>());
    if (text == "true" || text == "yes" || text == "oui" || text == "on" || text == "1") {
        out = true;
    } else if (text == "false" || text == "no" || text == "non" || text == "off" || text == "0") {
        out = false;
    } else {
        return false;
    }
    return true;
}

std::string inQuotes(const std::string& text) { return "\"" + text + "\""; }

// A source entry, or why it was skipped.
Result<ShopConfig> parseSource(const json& entry, const std::string& where, const std::string& defaultName) {
    ShopConfig source;
    source.name = defaultName;
    source.type.clear();
    if (entry.is_string()) {  // just the index URL
        source.url = entry.get<std::string>();
    } else if (!entry.is_object()) {
        return makeError(ErrorCode::ParseError, where + ": expected an object like {\"name\": ..., \"url\": ...}");
    } else {
        if (const json* name = field(entry, {"name", "label"}); name != nullptr && !asText(*name, source.name)) {
            return makeError(ErrorCode::ParseError, where + ": \"name\" must be text");
        }
        if (source.name.empty()) source.name = defaultName;
        const std::string label = where + " (" + inQuotes(source.name) + ")";
        struct Text {
            std::initializer_list<const char*> keys;
            std::string* target;
            const char* what;
            bool numbers;  // a password or user name typed without quotes
        };
        for (const Text& t : {Text{{"type"}, &source.type, "type", false},
                              Text{{"url", "index_url", "index", "address"}, &source.url, "url", false},
                              Text{{"username", "user", "login"}, &source.username, "username", true},
                              Text{{"password", "pass"}, &source.password, "password", true}}) {
            const json* value = field(entry, t.keys);
            if (value != nullptr && !(t.numbers ? asText(*value, *t.target) : asString(*value, *t.target))) {
                return makeError(ErrorCode::ParseError, label + ": \"" + t.what + "\" must be text");
            }
        }
        source.type = lower(source.type);
        if (source.type == "https" || source.type == "web") source.type = "http";
        if (source.type == "ftps" || source.type == "nas") source.type = "ftp";
        if (!source.type.empty() && source.type != "ftp" && source.type != "http" && source.type != "mock") {
            return makeError(ErrorCode::ParseError,
                             label + ": unknown type " + inQuotes(source.type) + " (ftp, http or mock)");
        }
    }
    auto trim = [](std::string& s) {
        s.erase(0, s.find_first_not_of(" \t\r\n"));
        s.erase(s.find_last_not_of(" \t\r\n") + 1);
    };
    trim(source.name);
    trim(source.url);
    if (source.name.empty()) source.name = defaultName;
    if (source.type.empty()) {  // from the URL: http(s):// is a web shop, anything else a NAS
        std::string scheme = lower(source.url.substr(0, source.url.find(':')));
        source.type = scheme == "http" || scheme == "https" ? "http" : "ftp";
    }
    source.verifyTls = source.type == "http";
    if (entry.is_object()) {
        if (const json* verify = field(entry, {"verifyTls", "verify_tls", "verify_ssl"});
            verify != nullptr && !asBool(*verify, source.verifyTls)) {
            return makeError(ErrorCode::ParseError,
                             where + " (" + inQuotes(source.name) + "): \"verifyTls\" must be true or false");
        }
    }
    return source;
}

}  // namespace

Result<AppConfig> parseConfig(std::string_view document) {
    const std::string relaxed = relaxJson(document);
    json root;
    try {
        root = json::parse(relaxed);
    } catch (const json::parse_error& e) {
        return makeError(ErrorCode::ParseError, "not valid JSON at " + position(relaxed, e.byte == 0 ? 0 : e.byte - 1));
    }
    if (!root.is_object()) return makeError(ErrorCode::ParseError, "configuration root must be a JSON object { ... }");

    if (auto it = root.find("version"); it != root.end() && it->is_number_integer()) {
        if (it->get<std::int64_t>() > AppConfig::kVersion) {
            return makeError(ErrorCode::Unsupported, "configuration written by a newer RetroManager");
        }
    }

    AppConfig config;
    auto warn = [&config](std::string text) { config.warnings.push_back(std::move(text)); };

    if (const json* saves = field(root, {"saves_url"}); saves != nullptr && !asString(*saves, config.savesUrl)) {
        warn("\"saves_url\" must be text: ignored");
    }
    if (const json* sysclk = field(root, {"sysclk"}); sysclk != nullptr) {
        if (!sysclk->is_object()) {
            warn("\"sysclk\" must be an object: ignored");
        } else {
            if (const json* enabled = field(*sysclk, {"enabled"}); enabled != nullptr && !asBool(*enabled, config.sysclk.enabled)) {
                warn("\"sysclk.enabled\" must be true or false: ignored");
            }
            std::string titleId;
            if (const json* id = field(*sysclk, {"title_id"}); id != nullptr) {
                bool hex16 = asString(*id, titleId) && titleId.size() == 16 &&
                             std::all_of(titleId.begin(), titleId.end(), [](unsigned char c) { return std::isxdigit(c) != 0; });
                if (hex16) {
                    config.sysclk.titleId = titleId;
                } else {
                    warn("\"sysclk.title_id\" must be 16 hexadecimal digits: ignored");
                }
            }
        }
    }
    if (const json* bundle = field(root, {"ca_bundle"}); bundle != nullptr && !asString(*bundle, config.caBundle)) {
        warn("\"ca_bundle\" must be text: ignored");
    }
    if (const json* scraper = field(root, {"scraper"}); scraper != nullptr) {
        if (!scraper->is_object()) {
            warn("\"scraper\" must be an object: ignored");
        } else {
            if (const json* enabled = field(*scraper, {"enabled"}); enabled != nullptr && !asBool(*enabled, config.scraper.enabled)) {
                warn("\"scraper.enabled\" must be true or false: ignored");
            }
            if (const json* base = field(*scraper, {"base_url"}); base != nullptr && !asString(*base, config.scraper.baseUrl)) {
                warn("\"scraper.base_url\" must be text: ignored");
            }
        }
    }

    // Sources: "sources" (current format; an array, a single object or a
    // bare URL), else the single "shop" of the first versions, which becomes
    // the "NAS" source.
    if (const json* sources = field(root, {"sources"}); sources != nullptr) {
        json list = sources->is_array() ? *sources : json::array({*sources});
        config.sources.clear();
        for (std::size_t i = 0; i < list.size(); ++i) {
            const std::string where = "sources[" + std::to_string(i) + "]";
            auto source = parseSource(list[i], where, "Source " + std::to_string(i + 1));
            if (!source) {
                warn(source.error().message + ": source skipped");
                continue;
            }
            ShopConfig entry = std::move(source).value();
            auto taken = [&config](const std::string& name) {
                return std::any_of(config.sources.begin(), config.sources.end(),
                                   [&](const ShopConfig& other) { return lower(other.name) == lower(name); });
            };
            if (taken(entry.name)) {
                std::string base = entry.name;
                for (int n = 2; taken(entry.name); ++n) entry.name = base + " (" + std::to_string(n) + ")";
                warn(where + ": the name " + inQuotes(base) + " is already used, renamed " + inQuotes(entry.name));
            }
            config.sources.push_back(std::move(entry));
        }
    } else if (const json* shop = field(root, {"shop"}); shop != nullptr) {
        auto source = parseSource(*shop, "shop", "NAS");
        if (source) {
            config.sources = {std::move(source).value()};
        } else {
            config.sources.clear();
            warn(source.error().message + ": source skipped");
        }
    }

    std::string active = config.sources.empty() ? "" : config.sources.front().name;
    if (const json* wanted = field(root, {"active_source"}); wanted != nullptr) {
        std::string name;
        if (!asString(*wanted, name)) {
            warn("\"active_source\" must be the name of a source: ignored");
        } else {
            for (const ShopConfig& source : config.sources) {
                if (lower(source.name) == lower(name)) active = source.name;  // deleted: the first one
            }
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
