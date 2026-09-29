#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "retromanager/core/Result.hpp"
#include "retromanager/forwarder/Aes128.hpp"

namespace rm::nsp {

// --- Keys -----------------------------------------------------------------

// The two console keys an NCA needs (from prod.keys, dumped with Lockpick_RCM).
// Never logged, never written anywhere.
struct NcaKeys {
    crypto::AesKey headerDataKey{};    // header_key, first half
    crypto::AesKey headerTweakKey{};   // header_key, second half
    crypto::AesKey keyAreaKey{};       // key_area_key_application_00
};

constexpr const char* kHeaderKeyName = "header_key";
constexpr const char* kKeyAreaKeyName = "key_area_key_application_00";

// "name = hex" lines; ';' and '#' start comments; names are case-insensitive.
// NotFound naming the missing key (never showing any value), ParseError for
// a key of the wrong length.
Result<NcaKeys> parseProdKeys(const std::string& text);

// --- NACP (control.nacp) --------------------------------------------------

constexpr std::size_t kNacpSize = 0x4000;
constexpr int kNacpLanguages = 16;
// icon_<name>.dat, in NACP title order.
const std::array<const char*, kNacpLanguages>& nacpLanguageNames();

struct NacpSpec {
    std::uint64_t titleId = 0;
    std::string name;       // cut to 0x1FF bytes on a UTF-8 boundary
    std::string publisher;  // cut to 0xFF bytes
    std::string displayVersion = "1.0.0";
};

Bytes buildNacp(const NacpSpec& spec);
std::string nacpName(const Bytes& nacp, int language = 0);
std::string nacpPublisher(const Bytes& nacp, int language = 0);

// --- CNMT (packaged content meta) -----------------------------------------

enum class ContentType : std::uint8_t { Program = 0, Meta = 1, Control = 2 };  // NCA header values
enum class CnmtContentType : std::uint8_t { Meta = 0, Program = 1, Data = 2, Control = 3 };

struct ContentRecord {
    crypto::Sha256Digest hash{};  // of the whole NCA; the content id is its first 16 bytes
    std::uint64_t size = 0;
    CnmtContentType type = CnmtContentType::Program;

    std::string contentId() const;  // 32 lowercase hex digits
};

struct CnmtInfo {
    std::uint64_t titleId = 0;
    std::uint32_t version = 0;
    std::uint8_t type = 0;
    std::uint64_t patchId = 0;
    std::vector<ContentRecord> contents;
};

constexpr std::uint8_t kCnmtApplication = 0x80;

Bytes buildApplicationCnmt(std::uint64_t titleId, std::uint32_t version, const std::vector<ContentRecord>& contents);
std::string cnmtFileName(std::uint64_t titleId);  // Application_0500000000001000.cnmt
Result<CnmtInfo> readCnmt(const Bytes& cnmt);

// --- NPDM -----------------------------------------------------------------

// Sets the program id of the stub's main.npdm (ACI0 program id and ACID
// allowed range) to `titleId`. ParseError when this is not an NPDM.
Result<Bytes> patchNpdmTitleId(Bytes npdm, std::uint64_t titleId);
Result<std::uint64_t> npdmTitleId(const Bytes& npdm);  // the ACI0 program id

// --- Title ids --------------------------------------------------------------

// 0x05XXXXXXXXXXX000 derived from `seed` (e.g. the ROM path): stable, in
// the homebrew range, low 12 bits clear (update = +0x800, DLC = +0x1000...).
std::uint64_t forwarderTitleId(const std::string& seed);
std::string titleIdHex(std::uint64_t titleId);  // 16 lowercase hex digits

}  // namespace rm::nsp
