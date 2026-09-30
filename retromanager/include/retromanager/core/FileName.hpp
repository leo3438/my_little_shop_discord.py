#pragma once

#include <string>
#include <string_view>

#include "retromanager/core/Result.hpp"

namespace rm {

// A single file or folder name made safe for FAT32 / exFAT: <>:"|?* and
// control characters become '_', leading spaces and trailing dots / spaces
// are dropped (FAT drops them silently). InvalidArgument for a path
// separator, a hidden name (leading dot, which would also collide with
// staging files), nothing usable left, or more than 255 bytes.
Result<std::string> sanitizeFileName(std::string_view name);

// A file name every tool on the console can list, for files the user hands
// to another program (forwarder NSPs for DBI / Tinfoil): accented Latin
// letters become their base letter ("Pokémon" -> "Pokemon", "ß" -> "ss"),
// apostrophes vanish, any other character becomes a space, then spaces are
// collapsed and trimmed. Only [A-Za-z0-9 -] remains, at most 200 bytes.
// Empty when nothing usable is left (a title in Japanese, say).
std::string asciiFileName(std::string_view utf8);

}  // namespace rm
