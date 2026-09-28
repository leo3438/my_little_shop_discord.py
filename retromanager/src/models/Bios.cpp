#include "retromanager/models/Bios.hpp"

#include <algorithm>
#include <cctype>

namespace rm::bios {

namespace {

bool equalsIgnoreCase(std::string_view a, std::string_view b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](unsigned char x, unsigned char y) {
               return std::tolower(x) == std::tolower(y);
           });
}

}  // namespace

const std::vector<BiosFile>& catalogue() {
    // Names and digests from the libretro core documentation. "required"
    // follows the cores shipped with RetroArch for Switch.
    static const std::vector<BiosFile> files = {
        {"gba", "gba_bios.bin", "a860e8c0b6d573d191e4ec7db1b1e4f6", "Game Boy Advance BIOS", false},
        {"nds", "bios7.bin", "df692a80a5b1bc90728bc3dfc76cd948", "Nintendo DS ARM7 BIOS (melonDS)", false},
        {"nds", "bios9.bin", "a392174eb3e572fed6447e956bde4b25", "Nintendo DS ARM9 BIOS (melonDS)", false},
        {"nds", "firmware.bin", "", "Nintendo DS firmware (melonDS)", false},
        {"nes", "disksys.rom", "ca30b50f880eb660a320674ed365ef7a", "Famicom Disk System BIOS (.fds)", false},
        {"pcengine", "syscard3.pce", "38179df8f4ac870017db21ebcbf53114", "PC Engine CD System Card 3.0", false},
        {"psx", "scph5500.bin", "8dd7d5296a650fac7319bce665a6a53c", "PlayStation BIOS (Japan)", false},
        {"psx", "scph5501.bin", "490f666e1afb15b7362b406ed1cea246", "PlayStation BIOS (USA)", true},
        {"psx", "scph5502.bin", "32736f17079d0b2b7024407c39bd3050", "PlayStation BIOS (Europe)", false},
    };
    return files;
}

std::vector<const BiosFile*> forSystem(std::string_view systemId) {
    std::vector<const BiosFile*> out;
    for (const BiosFile& file : catalogue()) {
        if (equalsIgnoreCase(file.system, systemId)) out.push_back(&file);
    }
    return out;
}

const BiosFile* find(std::string_view fileName) {
    for (const BiosFile& file : catalogue()) {
        if (equalsIgnoreCase(file.fileName, fileName)) return &file;
    }
    return nullptr;
}

bool isSafeFileName(std::string_view fileName) {
    if (fileName.empty() || fileName.front() == '.') return false;
    return std::none_of(fileName.begin(), fileName.end(), [](unsigned char c) {
        return c == '/' || c == '\\' || c == ':' || c < 0x20;
    });
}

}  // namespace rm::bios
