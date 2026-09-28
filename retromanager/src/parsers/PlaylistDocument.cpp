#include "retromanager/parsers/PlaylistDocument.hpp"

#include <algorithm>
#include <cctype>

namespace rm {

namespace {

using Json = nlohmann::ordered_json;

bool isBlank(std::string_view text) {
    return std::all_of(text.begin(), text.end(), [](unsigned char c) { return std::isspace(c) != 0; });
}

std::string lower(std::string_view text) {
    std::string out(text);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

// "sdmc:/ROMS/nds/a.nds" and "/roms/nds/A.nds" are the same file.
std::string pathKey(std::string_view path) {
    std::string key = lower(path);
    if (key.rfind("sdmc:", 0) == 0) key.erase(0, 5);
    return key;
}

const char* const kItemFields[] = {"path", "label", "core_path", "core_name", "crc32", "db_name"};

Json toJson(const PlaylistItem& item) {
    Json entry = Json::object();
    entry["path"] = item.path;
    entry["label"] = item.label;
    entry["core_path"] = item.corePath;
    entry["core_name"] = item.coreName;
    entry["crc32"] = item.crc32;
    entry["db_name"] = item.dbName;
    return entry;
}

std::string field(const Json& entry, const char* key) {
    auto it = entry.find(key);
    return it != entry.end() ? it->get<std::string>() : std::string();
}

Result<std::vector<std::string>> legacyLines(std::string_view content) {
    std::vector<std::string> lines;
    std::size_t begin = 0;
    while (begin <= content.size()) {
        std::size_t end = content.find('\n', begin);
        if (end == std::string_view::npos) end = content.size();
        std::string line(content.substr(begin, end - begin));
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines.push_back(std::move(line));
        begin = end + 1;
    }
    while (!lines.empty() && lines.back().empty()) lines.pop_back();
    if (lines.size() % 6 != 0) {
        return makeError(ErrorCode::ParseError, "legacy playlist: " + std::to_string(lines.size()) +
                                                    " lines, expected six per entry");
    }
    return lines;
}

}  // namespace

PlaylistDocument::PlaylistDocument() {
    json_ = Json::object();
    json_["version"] = "1.5";
    json_["default_core_path"] = "";
    json_["default_core_name"] = "";
    json_["label_display_mode"] = 0;
    json_["right_thumbnail_mode"] = 0;
    json_["left_thumbnail_mode"] = 0;
    json_["sort_mode"] = 0;
    json_["items"] = Json::array();
}

Result<PlaylistDocument> PlaylistDocument::parse(std::string_view content) {
    PlaylistDocument doc;
    if (isBlank(content)) return doc;

    std::size_t first = content.find_first_not_of(" \t\r\n");
    if (content[first] != '{') {
        auto lines = legacyLines(content);
        if (!lines) return lines.error();
        for (std::size_t i = 0; i < lines.value().size(); i += 6) {
            const auto& l = lines.value();
            doc.json_["items"].push_back(toJson(PlaylistItem{l[i], l[i + 1], l[i + 2], l[i + 3], l[i + 4], l[i + 5]}));
        }
        doc.legacy_ = true;
        return doc;
    }

    Json parsed = Json::parse(content.begin(), content.end(), nullptr, /*allow_exceptions=*/false);
    if (parsed.is_discarded() || !parsed.is_object()) {
        return makeError(ErrorCode::ParseError, "not a RetroArch playlist (invalid JSON)");
    }
    if (!parsed.contains("items")) parsed["items"] = Json::array();
    if (!parsed["items"].is_array()) return makeError(ErrorCode::ParseError, "playlist \"items\" is not an array");
    for (const Json& entry : parsed["items"]) {
        if (!entry.is_object()) return makeError(ErrorCode::ParseError, "playlist entry is not an object");
        for (const char* key : kItemFields) {
            auto it = entry.find(key);
            if (it != entry.end() && !it->is_string()) {
                return makeError(ErrorCode::ParseError, std::string("playlist entry field \"") + key + "\" is not a string");
            }
        }
    }
    doc.json_ = std::move(parsed);
    return doc;
}

std::string PlaylistDocument::crcField(std::string_view crc32Hex) {
    bool valid = crc32Hex.size() == 8 &&
                 std::all_of(crc32Hex.begin(), crc32Hex.end(), [](unsigned char c) { return std::isxdigit(c) != 0; });
    if (!valid) return "DETECT";
    std::string upper(crc32Hex);
    std::transform(upper.begin(), upper.end(), upper.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return upper + "|crc";
}

std::vector<PlaylistItem> PlaylistDocument::items() const {
    std::vector<PlaylistItem> out;
    for (const Json& entry : json_["items"]) {
        out.push_back(PlaylistItem{field(entry, "path"), field(entry, "label"), field(entry, "core_path"),
                                   field(entry, "core_name"), field(entry, "crc32"), field(entry, "db_name")});
    }
    return out;
}

bool PlaylistDocument::upsert(const PlaylistItem& item) {
    const std::string key = pathKey(item.path);
    for (Json& entry : json_["items"]) {
        if (pathKey(field(entry, "path")) != key) continue;
        bool changed = false;
        auto refresh = [&](const char* name, const std::string& value) {
            if (field(entry, name) != value) {
                entry[name] = value;
                changed = true;
            }
        };
        auto fillIfMissing = [&](const char* name, const std::string& value) {
            if (field(entry, name).empty()) refresh(name, value);
        };
        fillIfMissing("label", item.label);
        fillIfMissing("core_path", item.corePath);
        fillIfMissing("core_name", item.coreName);
        if (item.crc32 != "DETECT") refresh("crc32", item.crc32);  // a known CRC beats an unknown one
        fillIfMissing("crc32", item.crc32);
        refresh("db_name", item.dbName);
        return changed;
    }
    json_["items"].push_back(toJson(item));
    return true;
}

std::string PlaylistDocument::serialize() const { return json_.dump(2) + "\n"; }

}  // namespace rm
