#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <vector>

#include "retromanager/core/Result.hpp"

namespace rm {

// One entry of a RetroArch playlist.
struct PlaylistItem {
    std::string path;               // content path as RetroArch opens it ("/roms/nds/Game.nds")
    std::string label;              // shown in the menu; also names the thumbnails
    std::string corePath = "DETECT";  // "DETECT": RetroArch asks / uses the playlist's default core
    std::string coreName = "DETECT";
    std::string crc32 = "DETECT";   // "1A2B3C4D|crc", or "DETECT"
    std::string dbName;             // "<playlist file name>", e.g. "Nintendo - Nintendo DS.lpl"

    bool operator==(const PlaylistItem& o) const {
        return path == o.path && label == o.label && corePath == o.corePath && coreName == o.coreName &&
               crc32 == o.crc32 && dbName == o.dbName;
    }
};

// A RetroArch playlist (.lpl), edited without losing anything RetroArch
// wrote: unknown fields (runtime stats, scan settings, entry_slot...), key
// order and other entries are kept as they are.
//
// Reads the JSON format (RetroArch ≥ 1.7.6) and the legacy six-lines-per-
// entry text format; always writes JSON, 2-space indented like RetroArch.
class PlaylistDocument {
  public:
    // "" (or blanks) = a new, empty playlist. ParseError on anything else
    // that is not a playlist: the caller must then leave the file alone.
    static Result<PlaylistDocument> parse(std::string_view content);

    // "1a2b3c4d" -> "1A2B3C4D|crc"; anything else -> "DETECT".
    static std::string crcField(std::string_view crc32Hex);

    std::vector<PlaylistItem> items() const;
    bool wasLegacy() const { return legacy_; }

    // Adds the item, or updates the entry for the same file (paths compared
    // ignoring case and an "sdmc:" prefix, as FAT and RetroArch do). An
    // existing entry keeps its path spelling, label and core association
    // (the user may have renamed it or picked a core); only its CRC and
    // database name are refreshed. Returns whether anything changed.
    bool upsert(const PlaylistItem& item);

    std::string serialize() const;

  private:
    PlaylistDocument();

    nlohmann::ordered_json json_;
    bool legacy_ = false;
};

}  // namespace rm
