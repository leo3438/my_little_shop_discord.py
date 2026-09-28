#pragma once

#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "retromanager/models/Bios.hpp"

namespace rm {

// One downloadable game, as advertised by a shop index.
//
// Models are plain data shared by every layer, the UI included. They carry
// no behaviour and no dependency beyond the STL.
struct GameEntry {
    std::string id;           // unique within an index; "<system>/<fileName>" when not provided
    std::string title;        // display name
    std::string system;       // lowercase system id, see Systems.hpp ("snes", "gba"...); "unknown" if undetermined
    std::string region;       // free form ("EUR", "USA", "JPN"...), may be empty
    std::uint64_t sizeBytes = 0;  // 0 = unknown
    std::string romUrl;       // absolute URL (ftp://, http(s)://, smb://), fragment stripped
    std::string fileName;     // name the file will get on the SD card
    std::string boxartUrl;    // absolute URL, may be empty
    std::string crc32;        // 8 lowercase hex digits, may be empty
    std::optional<int> year;
    std::string description;
    std::string cheatUrl;     // absolute URL of a RetroArch .cht file, may be empty

    bool operator==(const GameEntry& other) const {
        return id == other.id && title == other.title && system == other.system && region == other.region &&
               sizeBytes == other.sizeBytes && romUrl == other.romUrl && fileName == other.fileName &&
               boxartUrl == other.boxartUrl && crc32 == other.crc32 && year == other.year &&
               description == other.description && cheatUrl == other.cheatUrl;
    }
};

// A parsed shop index.
struct RepoIndex {
    std::string name;     // shop name, may be empty
    std::string motd;     // message of the day (Tinfoil's "success" field), may be empty
    std::vector<GameEntry> games;
    std::vector<BiosEntry> bios;  // "bios" section: system files the shop can provide
    // Non-fatal problems: skipped entries, ignored fields. Worth logging,
    // never worth refusing the whole shop over.
    std::vector<std::string> warnings;
};

// What the shop screen shows: the index plus what the SD card already has.
struct ShopListing {
    RepoIndex index;
    std::set<std::string> installedIds;  // GameEntry::id of the games whose ROM is on the card
};

// Games of one system, as displayed in a sectioned list.
struct SystemSection {
    std::string system;       // id
    std::string displayName;  // "Super Nintendo"
    std::vector<GameEntry> games;
};

}  // namespace rm
