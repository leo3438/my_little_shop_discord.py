#include "retromanager/core/Format.hpp"

#include <cstdio>

namespace rm {

std::string formatBytes(std::uint64_t bytes) {
    static const char* kUnits[] = {"B", "KB", "MB", "GB", "TB"};
    if (bytes < 1024) return std::to_string(bytes) + " B";

    double value = static_cast<double>(bytes);
    std::size_t unit = 0;
    while (value >= 1024.0 && unit + 1 < sizeof(kUnits) / sizeof(kUnits[0])) {
        value /= 1024.0;
        ++unit;
    }

    // One decimal below 100, none above; drop a useless ".0".
    char buffer[32];
    if (value < 100.0) {
        std::snprintf(buffer, sizeof(buffer), "%.1f", value);
        std::string text(buffer);
        if (text.size() > 2 && text.compare(text.size() - 2, 2, ".0") == 0) text.resize(text.size() - 2);
        return text + " " + kUnits[unit];
    }
    std::snprintf(buffer, sizeof(buffer), "%.0f", value);
    return std::string(buffer) + " " + kUnits[unit];
}

}  // namespace rm
