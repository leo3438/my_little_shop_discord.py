#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace rm {

// Human readable size with binary multiples and the labels the Switch uses:
// 512 -> "512 B", 1536 -> "1.5 KB", 134217728 -> "128 MB".
std::string formatBytes(std::uint64_t bytes);

// Inserts line breaks in runs longer than `maxRun` bytes without a space
// (URLs), preferably right after '/', '.', '?', '&', '=' or '-', never inside
// a UTF-8 character: the UI wraps text at spaces only, so a long address
// would otherwise run off the screen.
std::string breakLongLines(const std::string& text, std::size_t maxRun);

}  // namespace rm
