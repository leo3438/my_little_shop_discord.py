#pragma once

#include <cstdint>
#include <vector>

#include "retromanager/core/Result.hpp"
#include "retromanager/forwarder/nsp/Containers.hpp"
#include "retromanager/forwarder/nsp/Metadata.hpp"

namespace rm::nsp {

// --- NCA ------------------------------------------------------------------
// NCA3, standard crypto (no rights id, no ticket), key generation 0
// (key_area_key_application_00, accepted by every firmware), sections
// AES-CTR encrypted with a per-NCA content key stored in the key area.

struct NcaSection {
    enum class Kind { Pfs0, RomFs };
    Kind kind = Kind::Pfs0;
    Bytes payload;  // the PFS0 or RomFS image
};

struct NcaSpec {
    ContentType type = ContentType::Program;
    std::uint64_t titleId = 0;
    std::vector<NcaSection> sections;  // at most 4
    crypto::AesKey contentKey{};       // random in production
    std::uint32_t sdkVersion = 0x000C1100;
};

Bytes buildNca(const NcaSpec& spec, const NcaKeys& keys);

struct NcaContents {
    ContentType type = ContentType::Program;
    std::uint64_t titleId = 0;
    std::uint64_t contentSize = 0;
    std::vector<NcaSection> sections;  // decrypted payloads, every hash checked
};

// ParseError / IntegrityError when anything does not match.
Result<NcaContents> readNca(const Bytes& nca, const NcaKeys& keys);

// --- Application NSP ------------------------------------------------------

struct ApplicationSpec {
    std::uint64_t titleId = 0;
    std::uint32_t version = 0;
    std::vector<NamedFile> exefs;  // main + main.npdm; the NPDM's program id is set to titleId
    std::vector<NamedFile> romfs;  // program RomFS, root files
    std::vector<NamedFile> logo;   // optional (NintendoLogo.png, StartupMovie.gif)
    Bytes nacp;
    Bytes icon;                    // JPEG, used for every language
};

struct ContentKeys {
    crypto::AesKey program{}, control{}, meta{};
};

// Program + Control + Meta NCAs in a PFS0 named <content id>.nca /
// <content id>.cnmt.nca, what DBI, Tinfoil or Awoo install.
Result<Bytes> buildApplicationNsp(const ApplicationSpec& spec, const NcaKeys& keys, const ContentKeys& contentKeys);

struct ApplicationContents {
    std::uint64_t titleId = 0;
    std::vector<std::string> fileNames;  // of the NSP
    std::vector<NamedFile> exefs, romfs, control;
    CnmtInfo cnmt;
};

// Reads an NSP back and checks it end to end: every NCA hash, content id,
// size and type against the CNMT, title ids everywhere.
Result<ApplicationContents> readApplicationNsp(const Bytes& nsp, const NcaKeys& keys);

}  // namespace rm::nsp
