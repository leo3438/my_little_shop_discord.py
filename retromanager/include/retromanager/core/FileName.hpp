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

}  // namespace rm
