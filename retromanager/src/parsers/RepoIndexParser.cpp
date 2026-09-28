#include "retromanager/parsers/RepoIndexParser.hpp"

#include <algorithm>
#include <cctype>
#include <nlohmann/json.hpp>
#include <optional>
#include <unordered_set>

#include "retromanager/core/Url.hpp"
#include "retromanager/models/Systems.hpp"

namespace rm {

namespace {

using json = nlohmann::json;

constexpr int kMinYear = 1950;
constexpr int kMaxYear = 2100;

std::string trim(std::string_view value) {
    std::size_t begin = 0;
    std::size_t end = value.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(value[begin]))) ++begin;
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1]))) --end;
    return std::string(value.substr(begin, end - begin));
}

std::string toLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

std::string stemOf(const std::string& fileName) {
    std::size_t dot = fileName.rfind('.');
    return (dot == std::string::npos || dot == 0) ? fileName : fileName.substr(0, dot);
}

bool isHex8(const std::string& value) {
    return value.size() == 8 &&
           std::all_of(value.begin(), value.end(), [](unsigned char c) { return std::isxdigit(c) != 0; });
}

// Reads an optional string field. Returns false (and sets `problem`) when
// the field exists with the wrong type.
bool readString(const json& entry, const char* key, std::string& out, std::string& problem) {
    auto it = entry.find(key);
    if (it == entry.end() || it->is_null()) return true;
    if (!it->is_string()) {
        problem = std::string("\"") + key + "\" must be a string";
        return false;
    }
    out = it->get<std::string>();
    return true;
}

class EntryParser {
  public:
    EntryParser(const std::string& baseUrl, RepoIndex& index) : baseUrl_(baseUrl), index_(index) {}

    void parseArray(const json& array, const std::string& arrayName) {
        for (std::size_t i = 0; i < array.size(); ++i) {
            std::string where = arrayName + "[" + std::to_string(i) + "]";
            std::string problem;
            std::optional<GameEntry> game = parseEntry(array[i], problem);
            if (!game) {
                index_.warnings.push_back(where + " skipped: " + problem);
                continue;
            }
            if (!seenIds_.insert(game->id).second) {
                index_.warnings.push_back(where + " skipped: duplicate id \"" + game->id + "\"");
                continue;
            }
            index_.games.push_back(std::move(*game));
        }
    }

  private:
    std::optional<GameEntry> parseEntry(const json& entry, std::string& problem) {
        if (!entry.is_object()) {
            problem = "entry must be an object";
            return std::nullopt;
        }

        GameEntry game;
        std::string rawUrl, rawBoxart, rawCheat, title, system;
        if (!readString(entry, "url", rawUrl, problem) || !readString(entry, "title", title, problem) ||
            !readString(entry, "system", system, problem) || !readString(entry, "region", game.region, problem) ||
            !readString(entry, "boxart", rawBoxart, problem) || !readString(entry, "cheat_url", rawCheat, problem) ||
            !readString(entry, "id", game.id, problem) ||
            !readString(entry, "crc32", game.crc32, problem) ||
            !readString(entry, "description", game.description, problem)) {
            return std::nullopt;
        }

        // URL (required). Tinfoil convention: "#name" gives the file name.
        if (trim(rawUrl).empty()) {
            problem = "missing \"url\"";
            return std::nullopt;
        }
        auto resolved = url::resolve(baseUrl_, trim(rawUrl));
        if (!resolved) {
            problem = resolved.error().message;
            return std::nullopt;
        }
        std::string fragment = url::fragment(resolved.value());
        game.romUrl = url::stripFragment(resolved.value());
        game.fileName = fragment.empty() ? url::lastPathSegment(game.romUrl) : url::percentDecode(fragment);
        game.fileName = trim(game.fileName);
        if (game.fileName.empty() || game.fileName.find('/') != std::string::npos) {
            problem = "cannot derive a file name from \"url\"";
            return std::nullopt;
        }

        // Title
        if (entry.contains("title")) {
            game.title = trim(title);
            if (game.title.empty()) {
                problem = "\"title\" is blank";
                return std::nullopt;
            }
        } else {
            game.title = stemOf(game.fileName);
        }

        // System
        game.system = toLower(trim(system));
        if (game.system.empty()) game.system = systems::fromFileName(game.fileName);
        if (game.system.empty()) game.system = "unknown";

        // Size
        if (auto it = entry.find("size"); it != entry.end() && !it->is_null()) {
            if (it->is_number_unsigned()) {
                game.sizeBytes = it->get<std::uint64_t>();
            } else {
                problem = "\"size\" must be a non-negative integer";
                return std::nullopt;
            }
        }

        // Year
        if (auto it = entry.find("year"); it != entry.end() && !it->is_null()) {
            if (!it->is_number_integer() || it->get<std::int64_t>() < kMinYear || it->get<std::int64_t>() > kMaxYear) {
                problem = "\"year\" must be an integer between " + std::to_string(kMinYear) + " and " +
                          std::to_string(kMaxYear);
                return std::nullopt;
            }
            game.year = static_cast<int>(it->get<std::int64_t>());
        }

        // CRC32
        game.crc32 = toLower(trim(game.crc32));
        if (!game.crc32.empty() && !isHex8(game.crc32)) {
            problem = "\"crc32\" must be 8 hexadecimal digits";
            return std::nullopt;
        }

        // Boxart (optional, relative allowed)
        if (!trim(rawBoxart).empty()) {
            auto boxart = url::resolve(baseUrl_, trim(rawBoxart));
            if (!boxart) {
                problem = "\"boxart\": " + boxart.error().message;
                return std::nullopt;
            }
            game.boxartUrl = boxart.value();
        }

        // Cheat file (optional, relative allowed)
        if (!trim(rawCheat).empty()) {
            auto cheat = url::resolve(baseUrl_, trim(rawCheat));
            if (!cheat) {
                problem = "\"cheat_url\": " + cheat.error().message;
                return std::nullopt;
            }
            game.cheatUrl = url::stripFragment(cheat.value());
        }

        game.region = trim(game.region);
        game.id = trim(game.id);
        // Two entries mapping to the same /roms/<system>/<file> collide on
        // the SD card: the derived id makes that a duplicate.
        if (game.id.empty()) game.id = game.system + "/" + game.fileName;
        return game;
    }

    const std::string& baseUrl_;
    RepoIndex& index_;
    std::unordered_set<std::string> seenIds_;
};

Status readMetadata(const json& root, const char* key, std::string& out) {
    auto it = root.find(key);
    if (it == root.end() || it->is_null()) return success();
    if (!it->is_string()) return makeError(ErrorCode::ParseError, std::string("\"") + key + "\" must be a string");
    out = it->get<std::string>();
    return success();
}

}  // namespace

RepoIndexParser::RepoIndexParser(std::string baseUrl) : baseUrl_(std::move(baseUrl)) {}

Result<RepoIndex> RepoIndexParser::parse(std::string_view document) const {
    if (document.substr(0, 3) == "\xEF\xBB\xBF") document.remove_prefix(3);

    json root;
    try {
        root = json::parse(document.begin(), document.end());
    } catch (const json::parse_error& e) {
        return makeError(ErrorCode::ParseError, e.what());
    }

    if (!root.is_object()) return makeError(ErrorCode::ParseError, "index root must be a JSON object");

    if (auto it = root.find("version"); it != root.end()) {
        if (!it->is_number_integer() || it->get<std::int64_t>() < 1) {
            return makeError(ErrorCode::ParseError, "\"version\" must be a positive integer");
        }
        if (it->get<std::int64_t>() > kSupportedVersion) {
            return makeError(ErrorCode::Unsupported, "index format version " + std::to_string(it->get<std::int64_t>()) +
                                                         " is newer than supported (" +
                                                         std::to_string(kSupportedVersion) + ")");
        }
    }

    RepoIndex index;
    if (Status s = readMetadata(root, "name", index.name); !s) return s.error();
    if (Status s = readMetadata(root, "success", index.motd); !s) return s.error();

    auto games = root.find("games");
    auto files = root.find("files");
    if (games == root.end() && files == root.end()) {
        return makeError(ErrorCode::ParseError, "index has neither a \"games\" nor a \"files\" array");
    }
    if (games != root.end() && !games->is_array()) return makeError(ErrorCode::ParseError, "\"games\" must be an array");
    if (files != root.end() && !files->is_array()) return makeError(ErrorCode::ParseError, "\"files\" must be an array");

    EntryParser entries(baseUrl_, index);
    if (games != root.end()) entries.parseArray(*games, "games");
    if (files != root.end()) entries.parseArray(*files, "files");

    if (root.contains("directories")) {
        index.warnings.push_back("\"directories\" (Tinfoil sub-indexes) is not supported yet and was ignored");
    }
    return index;
}

}  // namespace rm
