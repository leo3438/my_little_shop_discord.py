#include "retromanager/parsers/EntryJson.hpp"

#include <nlohmann/json.hpp>

namespace rm {

namespace {

using Json = nlohmann::json;

Json gameObject(const GameEntry& g) {
    Json j = {{"id", g.id},       {"title", g.title},       {"system", g.system}, {"region", g.region},
              {"size", g.sizeBytes}, {"url", g.romUrl},     {"file", g.fileName}, {"boxart", g.boxartUrl},
              {"crc32", g.crc32}, {"description", g.description}, {"cheat_url", g.cheatUrl}};
    if (g.year) j["year"] = *g.year;
    return j;
}

Json appObject(const AppEntry& a) {
    return {{"id", a.id},
            {"title", a.title},
            {"author", a.author},
            {"version", a.version},
            {"description", a.description},
            {"nro_url", a.nroUrl},
            {"icon_url", a.iconUrl},
            {"folder", a.folder},
            {"size", a.sizeBytes},
            {"crc32", a.crc32},
            {"category", a.category == AppCategory::Emulator ? "emulator" : "homebrew"}};
}

Result<Json> parseObject(std::string_view text) {
    Json j = Json::parse(text.begin(), text.end(), nullptr, /*allow_exceptions=*/false);
    if (j.is_discarded() || !j.is_object()) return makeError(ErrorCode::ParseError, "not a JSON object");
    return j;
}

// Missing or mistyped fields read as empty: the entry was written by us.
std::string text(const Json& j, const char* key) {
    auto it = j.find(key);
    return it != j.end() && it->is_string() ? it->get<std::string>() : std::string();
}

std::uint64_t number(const Json& j, const char* key) {
    auto it = j.find(key);
    return it != j.end() && it->is_number_unsigned() ? it->get<std::uint64_t>() : 0;
}

Result<GameEntry> gameFrom(const Json& j) {
    GameEntry g;
    g.id = text(j, "id");
    g.title = text(j, "title");
    g.system = text(j, "system");
    g.region = text(j, "region");
    g.sizeBytes = number(j, "size");
    g.romUrl = text(j, "url");
    g.fileName = text(j, "file");
    g.boxartUrl = text(j, "boxart");
    g.crc32 = text(j, "crc32");
    g.description = text(j, "description");
    g.cheatUrl = text(j, "cheat_url");
    if (auto it = j.find("year"); it != j.end() && it->is_number_integer()) g.year = it->get<int>();
    if (g.romUrl.empty() || g.fileName.empty()) return makeError(ErrorCode::ParseError, "game without url or file name");
    return g;
}

Result<AppEntry> appFrom(const Json& j) {
    AppEntry a;
    a.id = text(j, "id");
    a.title = text(j, "title");
    a.author = text(j, "author");
    a.version = text(j, "version");
    a.description = text(j, "description");
    a.nroUrl = text(j, "nro_url");
    a.iconUrl = text(j, "icon_url");
    a.folder = text(j, "folder");
    a.sizeBytes = number(j, "size");
    a.crc32 = text(j, "crc32");
    a.category = text(j, "category") == "emulator" ? AppCategory::Emulator : AppCategory::Homebrew;
    if (a.id.empty() || a.nroUrl.empty() || a.folder.empty()) {
        return makeError(ErrorCode::ParseError, "application without id, nro_url or folder");
    }
    return a;
}

}  // namespace

std::string gameToJson(const GameEntry& game) { return gameObject(game).dump(); }
std::string appToJson(const AppEntry& app) { return appObject(app).dump(); }

Result<GameEntry> gameFromJson(std::string_view json) {
    auto j = parseObject(json);
    if (!j) return j.error();
    return gameFrom(j.value());
}

Result<AppEntry> appFromJson(std::string_view json) {
    auto j = parseObject(json);
    if (!j) return j.error();
    return appFrom(j.value());
}

std::string queuePayload(const GameEntry& game) { return Json{{"kind", "rom"}, {"entry", gameObject(game)}}.dump(); }
std::string queuePayload(const AppEntry& app) { return Json{{"kind", "app"}, {"entry", appObject(app)}}.dump(); }

}  // namespace rm
