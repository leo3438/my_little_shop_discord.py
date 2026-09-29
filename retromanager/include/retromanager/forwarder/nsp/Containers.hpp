#pragma once

// Nintendo Switch container formats needed to package a forwarder, written
// from the public format documentation (switchbrew): this directory is the
// "hacbrewpack-like" part of RetroManager and depends on nothing else in the
// app but Result and the crypto primitives.

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "retromanager/core/Result.hpp"
#include "retromanager/forwarder/Sha256.hpp"

namespace rm::nsp {

struct NamedFile {
    std::string name;
    Bytes data;
};

// --- PFS0 (ExeFS, meta NCA section, the NSP itself) -----------------------

struct Pfs0Entry {
    std::string name;
    std::uint64_t offset = 0;  // from the start of the PFS0 blob
    std::uint64_t size = 0;
};

Bytes buildPfs0(const std::vector<NamedFile>& files);
std::uint64_t pfs0HeaderSize(const std::vector<std::string>& names);  // where the first file's data starts
Result<std::vector<Pfs0Entry>> readPfs0(const Bytes& blob);
// The files of a PFS0, copied out.
Result<std::vector<NamedFile>> extractPfs0(const Bytes& blob);

// --- RomFS (program and control NCAs) -------------------------------------
// Files at the root only: all a forwarder needs (nextNroPath, nextArgv,
// control.nacp, icon_<Language>.dat).

Bytes buildRomFs(std::vector<NamedFile> files);
Result<std::vector<NamedFile>> readRomFs(const Bytes& blob);
// Looks `name` up through the file hash table, as the console's FS does.
std::optional<Bytes> findRomFsFile(const Bytes& blob, const std::string& name);

// --- Hash trees of NCA sections -------------------------------------------

// A section's content plus the hash data of its FS header (0xF8 bytes at
// header offset 0x8).
struct HashedSection {
    Bytes data;
    Bytes hashData;
};

// HierarchicalSha256 (PFS0 sections): a table of SHA-256 over `blockSize`
// blocks, then the PFS0 at the next 0x200 boundary.
HashedSection buildSha256Section(const Bytes& pfs0, std::uint32_t blockSize = 0x1000);
// The PFS0 back, after checking every hash.
Result<Bytes> verifySha256Section(const Bytes& data, const Bytes& hashData);

// HierarchicalIntegrity / IVFC (RomFS sections): 5 hash levels + the data,
// 0x4000-byte blocks.
HashedSection buildIvfcSection(const Bytes& romfs);
Result<Bytes> verifyIvfcSection(const Bytes& data, const Bytes& hashData);

}  // namespace rm::nsp
