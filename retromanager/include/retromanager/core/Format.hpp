#pragma once

#include <cstdint>
#include <string>

namespace rm {

// Human readable size with binary multiples and the labels the Switch uses:
// 512 -> "512 B", 1536 -> "1.5 KB", 134217728 -> "128 MB".
std::string formatBytes(std::uint64_t bytes);

}  // namespace rm
