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

std::string breakLongLines(const std::string& text, std::size_t maxRun) {
    if (maxRun == 0) return text;
    auto continuation = [&](std::size_t i) { return (static_cast<unsigned char>(text[i]) & 0xC0) == 0x80; };
    std::string out;
    std::size_t start = 0;  // of the current run (no space, no newline)
    for (std::size_t i = 0; i <= text.size(); ++i) {
        bool end = i == text.size() || text[i] == ' ' || text[i] == '\n';
        if (!end) continue;
        // The run text[start, i): cut it into pieces of at most maxRun bytes.
        std::size_t pos = start;
        while (i - pos > maxRun) {
            std::size_t cut = 0;
            for (std::size_t j = pos + maxRun; j > pos; --j) {  // cut after a separator
                char c = text[j - 1];
                if (c == '/' || c == '.' || c == '?' || c == '&' || c == '=' || c == '-') {
                    cut = j;
                    break;
                }
            }
            if (cut == 0) {
                cut = pos + maxRun;
                while (cut > pos + 1 && continuation(cut)) --cut;
            }
            out.append(text, pos, cut - pos);
            out += '\n';
            pos = cut;
        }
        out.append(text, pos, i - pos);
        if (i < text.size()) out += text[i];
        start = i + 1;
    }
    return out;
}

}  // namespace rm
