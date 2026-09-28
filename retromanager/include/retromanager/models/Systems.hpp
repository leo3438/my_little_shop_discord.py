#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace rm {

struct SystemInfo {
    std::string id;           // lowercase, stable: used in indexes and as /roms/<id>
    std::string displayName;
    std::vector<std::string> extensions;  // lowercase, with the dot
    // Folder name used by the libretro databases (cheats, thumbnails),
    // e.g. "Nintendo - Nintendo DS". Empty when there is no single one.
    std::string libretroName;
};

// Catalogue of the systems RetroManager knows about.
namespace systems {

const std::vector<SystemInfo>& all();

// nullptr when unknown. Case-insensitive.
const SystemInfo* find(std::string_view id);

// "Super Mario World.SFC" -> "snes". Empty when the extension is unknown or
// ambiguous (.zip, .7z, .iso...).
std::string fromFileName(std::string_view fileName);

// Display name, or the id itself for unknown systems.
std::string displayName(std::string_view id);

}  // namespace systems

}  // namespace rm
