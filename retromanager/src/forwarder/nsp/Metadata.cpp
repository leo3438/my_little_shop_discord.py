#include "retromanager/forwarder/nsp/Metadata.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <map>
#include <sstream>

#include "Le.hpp"

namespace rm::nsp {

using namespace le;

// ---------------------------------------------------------------------------
// prod.keys

namespace {

std::string trim(const std::string& s) {
    std::size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return {};
    std::size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// The key's bytes; the error message names the key, never its value.
Result<Bytes> hexKey(const std::map<std::string, std::string>& keys, const std::string& name, std::size_t size) {
    auto it = keys.find(name);
    if (it == keys.end()) return Error{ErrorCode::NotFound, "prod.keys has no " + name};
    const std::string& hex = it->second;
    if (hex.size() != size * 2) return Error{ErrorCode::ParseError, name + " in prod.keys has the wrong length"};
    Bytes out(size);
    for (std::size_t i = 0; i < size; ++i) {
        int v = 0;
        for (int j = 0; j < 2; ++j) {
            char c = hex[i * 2 + static_cast<std::size_t>(j)];
            int d = std::isdigit(static_cast<unsigned char>(c)) ? c - '0' : (c >= 'a' && c <= 'f') ? c - 'a' + 10 : -1;
            if (d < 0) return Error{ErrorCode::ParseError, name + " in prod.keys is not hexadecimal"};
            v = v * 16 + d;
        }
        out[i] = static_cast<std::uint8_t>(v);
    }
    return out;
}

}  // namespace

Result<NcaKeys> parseProdKeys(const std::string& text) {
    std::map<std::string, std::string> keys;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        std::size_t comment = line.find_first_of(";#");
        if (comment != std::string::npos) line.resize(comment);
        std::size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        keys[lower(trim(line.substr(0, eq)))] = lower(trim(line.substr(eq + 1)));
    }
    auto header = hexKey(keys, kHeaderKeyName, 32);
    if (!header) return header.error();
    auto keyArea = hexKey(keys, kKeyAreaKeyName, 16);
    if (!keyArea) return keyArea.error();
    NcaKeys out;
    std::copy_n(header.value().begin(), 16, out.headerDataKey.begin());
    std::copy_n(header.value().begin() + 16, 16, out.headerTweakKey.begin());
    std::copy_n(keyArea.value().begin(), 16, out.keyAreaKey.begin());
    return out;
}

// ---------------------------------------------------------------------------
// NACP

const std::array<const char*, kNacpLanguages>& nacpLanguageNames() {
    static const std::array<const char*, kNacpLanguages> names = {
        "AmericanEnglish", "BritishEnglish", "Japanese",      "French",     "German",
        "LatinAmericanSpanish", "Spanish",  "Italian",       "Dutch",      "CanadianFrench",
        "Portuguese",      "Russian",        "Korean",        "TraditionalChinese", "SimplifiedChinese",
        "BrazilianPortuguese"};
    return names;
}

namespace {

// At most `max` bytes, not splitting a UTF-8 sequence.
std::string cutUtf8(const std::string& s, std::size_t max) {
    if (s.size() <= max) return s;
    std::size_t end = max;
    while (end > 0 && (static_cast<unsigned char>(s[end]) & 0xC0) == 0x80) --end;
    return s.substr(0, end);
}

std::string cString(const Bytes& b, std::size_t at, std::size_t max) {
    const char* start = reinterpret_cast<const char*>(b.data() + at);
    return std::string(start, strnlen(start, max));
}

constexpr std::size_t kTitleSize = 0x300;

}  // namespace

Bytes buildNacp(const NacpSpec& spec) {
    Bytes nacp(kNacpSize, 0);
    std::string name = cutUtf8(spec.name, 0x1FF);
    std::string publisher = cutUtf8(spec.publisher, 0xFF);
    for (int lang = 0; lang < kNacpLanguages; ++lang) {
        std::size_t title = static_cast<std::size_t>(lang) * kTitleSize;
        putString(nacp, title, name, 0x1FF);
        putString(nacp, title + 0x200, publisher, 0xFF);
    }
    put32(nacp, 0x302C, (1u << kNacpLanguages) - 1);          // supported languages
    put64(nacp, 0x3038, spec.titleId);                        // presence group id
    std::fill(nacp.begin() + 0x3040, nacp.begin() + 0x3060, 0xFF);  // no age rating
    putString(nacp, 0x3060, cutUtf8(spec.displayVersion, 0xF), 0xF);
    put64(nacp, 0x3070, spec.titleId + 0x1000);               // add-on content base id
    put64(nacp, 0x3078, spec.titleId);                        // save data owner id
    put64(nacp, 0x30B0, spec.titleId);                        // local communication id
    return nacp;
}

std::string nacpName(const Bytes& nacp, int language) {
    return cString(nacp, static_cast<std::size_t>(language) * kTitleSize, 0x200);
}

std::string nacpPublisher(const Bytes& nacp, int language) {
    return cString(nacp, static_cast<std::size_t>(language) * kTitleSize + 0x200, 0x100);
}

// ---------------------------------------------------------------------------
// CNMT: 0x20 header, 0x10 application extended header, 0x38-byte content
// records {hash, content id, u48 size, type, id offset}, 0x20 digest.

std::string ContentRecord::contentId() const { return crypto::toHex(hash.data(), 16); }

std::string titleIdHex(std::uint64_t titleId) {
    char text[17];
    std::snprintf(text, sizeof text, "%016llx", static_cast<unsigned long long>(titleId));
    return text;
}

std::string cnmtFileName(std::uint64_t titleId) { return "Application_" + titleIdHex(titleId) + ".cnmt"; }

Bytes buildApplicationCnmt(std::uint64_t titleId, std::uint32_t version, const std::vector<ContentRecord>& contents) {
    Bytes cnmt(0x30 + 0x38 * contents.size() + 0x20, 0);
    put64(cnmt, 0x00, titleId);
    put32(cnmt, 0x08, version);
    cnmt[0x0C] = kCnmtApplication;
    put16(cnmt, 0x0E, 0x10);
    put16(cnmt, 0x10, static_cast<std::uint16_t>(contents.size()));
    put64(cnmt, 0x20, titleId + 0x800);  // patch id
    for (std::size_t i = 0; i < contents.size(); ++i) {
        std::size_t r = 0x30 + 0x38 * i;
        putBytes(cnmt, r, contents[i].hash.data(), 32);
        putBytes(cnmt, r + 0x20, contents[i].hash.data(), 16);
        for (int b = 0; b < 6; ++b) cnmt[r + 0x30 + static_cast<std::size_t>(b)] = static_cast<std::uint8_t>(contents[i].size >> (8 * b));
        cnmt[r + 0x36] = static_cast<std::uint8_t>(contents[i].type);
    }
    return cnmt;
}

Result<CnmtInfo> readCnmt(const Bytes& cnmt) {
    if (cnmt.size() < 0x20) return Error{ErrorCode::ParseError, "truncated CNMT"};
    CnmtInfo info;
    info.titleId = get64(cnmt, 0);
    info.version = get32(cnmt, 8);
    info.type = cnmt[0x0C];
    std::size_t ext = get16(cnmt, 0x0E);
    std::size_t count = get16(cnmt, 0x10);
    if (cnmt.size() < 0x20 + ext + 0x38 * count) return Error{ErrorCode::ParseError, "truncated CNMT"};
    if (ext >= 8) info.patchId = get64(cnmt, 0x20);
    for (std::size_t i = 0; i < count; ++i) {
        std::size_t r = 0x20 + ext + 0x38 * i;
        ContentRecord rec;
        std::copy_n(cnmt.begin() + static_cast<std::ptrdiff_t>(r), 32, rec.hash.begin());
        for (int b = 0; b < 6; ++b) rec.size |= static_cast<std::uint64_t>(cnmt[r + 0x30 + static_cast<std::size_t>(b)]) << (8 * b);
        rec.type = static_cast<CnmtContentType>(cnmt[r + 0x36]);
        info.contents.push_back(rec);
    }
    return info;
}

// ---------------------------------------------------------------------------
// NPDM: "META" header whose 0x70..0x7F give the ACI0 and ACID offsets and
// sizes. ACI0: program id at 0x10. ACID: "ACID" at 0x200 (after the
// signature and public key), program id range at 0x210 / 0x218.

namespace {

struct NpdmLayout {
    std::size_t aci0 = 0, acid = 0;
};

Result<NpdmLayout> npdmLayout(const Bytes& npdm) {
    if (npdm.size() < 0x80 || std::memcmp(npdm.data(), "META", 4) != 0) return Error{ErrorCode::ParseError, "main.npdm is not an NPDM"};
    std::uint64_t aci0 = get32(npdm, 0x70), aci0Size = get32(npdm, 0x74);
    std::uint64_t acid = get32(npdm, 0x78), acidSize = get32(npdm, 0x7C);
    if (aci0Size < 0x18 || acidSize < 0x220 || aci0 + aci0Size > npdm.size() || acid + acidSize > npdm.size()) {
        return Error{ErrorCode::ParseError, "main.npdm is truncated"};
    }
    if (std::memcmp(npdm.data() + aci0, "ACI0", 4) != 0 || std::memcmp(npdm.data() + acid + 0x200, "ACID", 4) != 0) {
        return Error{ErrorCode::ParseError, "main.npdm has no ACI0/ACID"};
    }
    return NpdmLayout{static_cast<std::size_t>(aci0), static_cast<std::size_t>(acid)};
}

}  // namespace

Result<Bytes> patchNpdmTitleId(Bytes npdm, std::uint64_t titleId) {
    auto layout = npdmLayout(npdm);
    if (!layout) return layout.error();
    put64(npdm, layout.value().aci0 + 0x10, titleId);
    put64(npdm, layout.value().acid + 0x210, titleId);
    put64(npdm, layout.value().acid + 0x218, titleId);
    return npdm;
}

Result<std::uint64_t> npdmTitleId(const Bytes& npdm) {
    auto layout = npdmLayout(npdm);
    if (!layout) return layout.error();
    return get64(npdm, layout.value().aci0 + 0x10);
}

// ---------------------------------------------------------------------------

std::uint64_t forwarderTitleId(const std::string& seed) {
    // 0x0100 | 32 bits of SHA-256(seed) | 0x0000. HOME menu only lists
    // applications whose id has this shape: application range, low 16 bits
    // clear (the update is id + 0x800, add-ons id + 0x1000 and up).
    auto digest = crypto::Sha256::of(seed.data(), seed.size());
    std::uint32_t middle = 0;
    for (int i = 0; i < 4; ++i) middle = (middle << 8) | digest[static_cast<std::size_t>(i)];
    if (middle == 0) middle = 1;  // 0x0100000000000000.. is system titles
    return 0x0100000000000000ull | (static_cast<std::uint64_t>(middle) << 16);
}

}  // namespace rm::nsp
