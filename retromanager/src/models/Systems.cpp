#include "retromanager/models/Systems.hpp"

#include <algorithm>
#include <cctype>

namespace rm::systems {

namespace {

std::string toLower(std::string_view value) {
    std::string out(value);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

}  // namespace

const std::vector<SystemInfo>& all() {
    // Only unambiguous extensions: archives and disc images are shared by
    // many systems and must be tagged explicitly in the index.
    static const std::vector<SystemInfo> catalogue = {
        {"nes", "Nintendo NES", {".nes", ".fds"}, "Nintendo - Nintendo Entertainment System"},
        {"snes", "Super Nintendo", {".sfc", ".smc"}, "Nintendo - Super Nintendo Entertainment System"},
        {"n64", "Nintendo 64", {".n64", ".z64", ".v64"}, "Nintendo - Nintendo 64"},
        {"gb", "Game Boy", {".gb"}, "Nintendo - Game Boy"},
        {"gbc", "Game Boy Color", {".gbc"}, "Nintendo - Game Boy Color"},
        {"gba", "Game Boy Advance", {".gba"}, "Nintendo - Game Boy Advance"},
        {"nds", "Nintendo DS", {".nds"}, "Nintendo - Nintendo DS"},
        {"mastersystem", "Sega Master System", {".sms"}, "Sega - Master System - Mark III"},
        {"megadrive", "Sega Mega Drive", {".md", ".gen", ".smd"}, "Sega - Mega Drive - Genesis"},
        {"gamegear", "Sega Game Gear", {".gg"}, "Sega - Game Gear"},
        {"pcengine", "PC Engine", {".pce"}, "NEC - PC Engine - TurboGrafx 16"},
        {"psx", "PlayStation", {".pbp"}, "Sony - PlayStation"},
        {"arcade", "Arcade", {}, ""},  // FBNeo and MAME databases differ
    };
    return catalogue;
}

const SystemInfo* find(std::string_view id) {
    std::string wanted = toLower(id);
    for (const SystemInfo& system : all()) {
        if (system.id == wanted) return &system;
    }
    return nullptr;
}

std::string fromFileName(std::string_view fileName) {
    std::size_t dot = fileName.rfind('.');
    if (dot == std::string_view::npos || dot == 0) return "";
    std::string extension = toLower(fileName.substr(dot));
    for (const SystemInfo& system : all()) {
        if (std::find(system.extensions.begin(), system.extensions.end(), extension) != system.extensions.end()) {
            return system.id;
        }
    }
    return "";
}

std::string displayName(std::string_view id) {
    const SystemInfo* system = find(id);
    return system != nullptr ? system->displayName : std::string(id);
}

}  // namespace rm::systems
