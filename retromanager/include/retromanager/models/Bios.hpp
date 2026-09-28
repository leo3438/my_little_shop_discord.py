#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace rm {

// A BIOS / firmware file a RetroArch core looks for in the system folder.
struct BiosFile {
    std::string system;       // system id (Systems.hpp)
    std::string fileName;     // exact name expected by the cores
    std::string md5;          // reference digest (libretro docs), lowercase; empty = varies between dumps
    std::string description;  // "PlayStation BIOS (USA)"
    bool required = false;    // false: the usual core has a fallback (HLE BIOS) or only some games need it
};

// A BIOS file offered by the shop ("bios" section of the index).
struct BiosEntry {
    std::string fileName;
    std::string system;  // may be empty when neither the index nor the catalogue says
    std::string url;     // absolute
    std::string md5;     // lowercase, may be empty
    std::uint64_t sizeBytes = 0;  // 0 = unknown
};

namespace bios {

// The files RetroManager checks for, grouped by system.
const std::vector<BiosFile>& catalogue();

std::vector<const BiosFile*> forSystem(std::string_view systemId);

// Case-insensitive (FAT). nullptr when unknown.
const BiosFile* find(std::string_view fileName);

// A plain file name (no folder, no "..", not hidden): anything else could
// write outside the system folder.
bool isSafeFileName(std::string_view fileName);

}  // namespace bios

}  // namespace rm
