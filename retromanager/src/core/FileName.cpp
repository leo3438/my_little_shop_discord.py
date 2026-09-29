#include "retromanager/core/FileName.hpp"

namespace rm {

namespace {

constexpr std::size_t kMaxFileNameBytes = 255;  // FAT32 / exFAT limit (UTF-16 units, bytes is stricter)

// Characters FAT and exFAT refuse in file names.
bool isFatReserved(unsigned char c) {
    return c < 0x20 || c == 0x7F || c == '<' || c == '>' || c == ':' || c == '"' || c == '|' || c == '?' || c == '*';
}

}  // namespace

Result<std::string> sanitizeFileName(std::string_view raw) {
    if (raw.find_first_of("/\\") != std::string_view::npos) {
        return makeError(ErrorCode::InvalidArgument, "name contains a path separator: \"" + std::string(raw) + "\"");
    }
    std::string name(raw);
    for (char& c : name) {
        if (isFatReserved(static_cast<unsigned char>(c))) c = '_';
    }
    while (!name.empty() && (name.back() == '.' || name.back() == ' ')) name.pop_back();
    while (!name.empty() && name.front() == ' ') name.erase(name.begin());

    if (name.empty() || name.front() == '.') {
        return makeError(ErrorCode::InvalidArgument, "unusable file name \"" + std::string(raw) + "\"");
    }
    if (name.size() > kMaxFileNameBytes) {
        return makeError(ErrorCode::InvalidArgument, "file name longer than 255 bytes: \"" + std::string(raw) + "\"");
    }
    return name;
}

}  // namespace rm
