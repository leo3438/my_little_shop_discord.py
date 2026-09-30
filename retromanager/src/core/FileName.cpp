#include "retromanager/core/FileName.hpp"

namespace rm {

namespace {

constexpr std::size_t kMaxFileNameBytes = 255;  // FAT32 / exFAT limit (UTF-16 units, bytes is stricter)

// Characters FAT and exFAT refuse in file names.
bool isFatReserved(unsigned char c) {
    return c < 0x20 || c == 0x7F || c == '<' || c == '>' || c == ':' || c == '"' || c == '|' || c == '?' || c == '*';
}

constexpr std::size_t kMaxAsciiNameBytes = 200;

// Next code point of a UTF-8 string; invalid or truncated sequences yield
// U+FFFD and consume one byte.
char32_t nextCodePoint(std::string_view text, std::size_t& at) {
    const auto byte = [&](std::size_t i) { return static_cast<unsigned char>(text[i]); };
    const unsigned char lead = byte(at);
    std::size_t length = lead < 0x80 ? 1 : (lead >> 5) == 0x6 ? 2 : (lead >> 4) == 0xE ? 3 : (lead >> 3) == 0x1E ? 4 : 0;
    if (length == 0 || at + length > text.size()) {
        ++at;
        return 0xFFFD;
    }
    char32_t cp = length == 1 ? lead : lead & (0x7F >> length);
    for (std::size_t i = 1; i < length; ++i) {
        if ((byte(at + i) & 0xC0) != 0x80) {
            ++at;
            return 0xFFFD;
        }
        cp = (cp << 6) | (byte(at + i) & 0x3F);
    }
    at += length;
    return cp;
}

// ASCII spelling of a Latin-1 Supplement / Latin Extended-A letter, or
// nullptr. Indexed from U+00C0.
const char* latinBase(char32_t cp) {
    static const char* const kTable[] = {
        // U+00C0
        "A", "A", "A", "A", "A", "A", "AE", "C", "E", "E", "E", "E", "I", "I", "I", "I",
        "D", "N", "O", "O", "O", "O", "O", nullptr, "O", "U", "U", "U", "U", "Y", "TH", "ss",
        "a", "a", "a", "a", "a", "a", "ae", "c", "e", "e", "e", "e", "i", "i", "i", "i",
        "d", "n", "o", "o", "o", "o", "o", nullptr, "o", "u", "u", "u", "u", "y", "th", "y",
        // U+0100
        "A", "a", "A", "a", "A", "a", "C", "c", "C", "c", "C", "c", "C", "c", "D", "d",
        "D", "d", "E", "e", "E", "e", "E", "e", "E", "e", "E", "e", "G", "g", "G", "g",
        "G", "g", "G", "g", "H", "h", "H", "h", "I", "i", "I", "i", "I", "i", "I", "i",
        "I", "i", "IJ", "ij", "J", "j", "K", "k", "k", "L", "l", "L", "l", "L", "l", "L",
        "l", "L", "l", "N", "n", "N", "n", "N", "n", "n", "N", "n", "O", "o", "O", "o",
        "O", "o", "OE", "oe", "R", "r", "R", "r", "R", "r", "S", "s", "S", "s", "S", "s",
        "S", "s", "T", "t", "T", "t", "T", "t", "U", "u", "U", "u", "U", "u", "U", "u",
        "U", "u", "U", "u", "W", "w", "Y", "y", "Y", "Z", "z", "Z", "z", "Z", "z", "s",
    };
    if (cp < 0xC0 || cp > 0x17F) return nullptr;
    return kTable[cp - 0xC0];
}

bool isApostrophe(char32_t cp) { return cp == '\'' || cp == '`' || cp == 0x2018 || cp == 0x2019 || cp == 0x00B4; }
bool isCombiningMark(char32_t cp) { return cp >= 0x0300 && cp <= 0x036F; }

}  // namespace

std::string asciiFileName(std::string_view utf8) {
    std::string out;
    bool pendingSpace = false;
    auto append = [&](const char* text) {
        if (pendingSpace && !out.empty()) out += ' ';
        pendingSpace = false;
        out += text;
    };
    for (std::size_t at = 0; at < utf8.size();) {
        const char32_t cp = nextCodePoint(utf8, at);
        if (isApostrophe(cp) || isCombiningMark(cp)) continue;  // "Link's" -> "Links", "e\u0301" -> "e"
        if ((cp >= '0' && cp <= '9') || (cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z') || cp == '-') {
            const char c[2] = {static_cast<char>(cp), '\0'};
            append(c);
        } else if (const char* base = latinBase(cp)) {
            append(base);
        } else {
            pendingSpace = true;  // space, punctuation, symbols, other scripts
        }
    }
    if (out.size() > kMaxAsciiNameBytes) {
        out.resize(kMaxAsciiNameBytes);
        while (!out.empty() && out.back() == ' ') out.pop_back();
    }
    return out;
}

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
