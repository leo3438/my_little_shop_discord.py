#pragma once

#include <cstdint>
#include <string>

namespace rm {

enum class AppCategory { Homebrew, Emulator };  // "apps" / "emulators" section of the index

// A Switch homebrew (.nro) offered by the shop: emulators, tools...
// Installed as /switch/<folder>/<folder>.nro, the standard hbmenu layout.
struct AppEntry {
    std::string id;           // "app/<folder>"
    std::string title;
    std::string author;       // may be empty
    std::string version;      // free form ("1.19.1", "v2.0-beta"), may be empty
    std::string description;  // may be empty
    std::string nroUrl;       // absolute
    std::string iconUrl;      // absolute, may be empty
    std::string folder;       // FAT-safe folder (and .nro) name: the index's "folder", else the title
    std::uint64_t sizeBytes = 0;  // 0 = unknown
    std::string crc32;            // 8 lowercase hex digits, may be empty
    AppCategory category = AppCategory::Homebrew;

    bool operator==(const AppEntry& o) const {
        return id == o.id && title == o.title && author == o.author && version == o.version &&
               description == o.description && nroUrl == o.nroUrl && iconUrl == o.iconUrl && folder == o.folder &&
               sizeBytes == o.sizeBytes && crc32 == o.crc32 && category == o.category;
    }
};

}  // namespace rm
