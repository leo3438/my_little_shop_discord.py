#include "retromanager/forwarder/nsp/Containers.hpp"

#include <algorithm>
#include <cstring>

#include "Le.hpp"

namespace rm::nsp {

using namespace le;

namespace {

Error corrupt(const std::string& what) { return Error{ErrorCode::ParseError, what}; }
Error mismatch(const std::string& what) { return Error{ErrorCode::IntegrityError, what}; }

bool fits(const Bytes& b, std::uint64_t offset, std::uint64_t size) { return offset <= b.size() && size <= b.size() - offset; }

}  // namespace

// ---------------------------------------------------------------------------
// PFS0: "PFS0", count, string table size, reserved; 0x18-byte entries
// {data offset, size, name offset, reserved}; names; data.

std::uint64_t pfs0HeaderSize(const std::vector<std::string>& names) {
    std::uint64_t strings = 0;
    for (const auto& n : names) strings += n.size() + 1;
    std::uint64_t fixed = 0x10 + 0x18 * names.size();
    return alignUp(fixed + strings, 0x20);
}

Bytes buildPfs0(const std::vector<NamedFile>& files) {
    std::vector<std::string> names;
    std::uint64_t dataSize = 0;
    for (const auto& f : files) {
        names.push_back(f.name);
        dataSize += f.data.size();
    }
    std::uint64_t header = pfs0HeaderSize(names);
    std::uint64_t stringsStart = 0x10 + 0x18 * files.size();
    Bytes out(header + dataSize, 0);
    putBytes(out, 0, "PFS0", 4);
    put32(out, 4, static_cast<std::uint32_t>(files.size()));
    put32(out, 8, static_cast<std::uint32_t>(header - stringsStart));
    std::uint64_t dataOffset = 0, nameOffset = 0;
    for (std::size_t i = 0; i < files.size(); ++i) {
        std::size_t entry = 0x10 + 0x18 * i;
        put64(out, entry, dataOffset);
        put64(out, entry + 8, files[i].data.size());
        put32(out, entry + 16, static_cast<std::uint32_t>(nameOffset));
        putString(out, stringsStart + nameOffset, files[i].name, files[i].name.size());
        putBytes(out, header + dataOffset, files[i].data.data(), files[i].data.size());
        dataOffset += files[i].data.size();
        nameOffset += files[i].name.size() + 1;
    }
    return out;
}

Result<std::vector<Pfs0Entry>> readPfs0(const Bytes& blob) {
    if (blob.size() < 0x10 || std::memcmp(blob.data(), "PFS0", 4) != 0) return corrupt("not a PFS0");
    std::uint64_t count = get32(blob, 4), strings = get32(blob, 8);
    std::uint64_t stringsStart = 0x10 + 0x18 * count;
    std::uint64_t dataStart = stringsStart + strings;
    if (!fits(blob, 0, dataStart)) return corrupt("truncated PFS0 header");
    std::vector<Pfs0Entry> entries;
    for (std::uint64_t i = 0; i < count; ++i) {
        std::size_t entry = static_cast<std::size_t>(0x10 + 0x18 * i);
        std::uint64_t offset = get64(blob, entry), size = get64(blob, entry + 8), name = get32(blob, entry + 16);
        if (name >= strings) return corrupt("PFS0 name out of range");
        const char* start = reinterpret_cast<const char*>(blob.data() + stringsStart + name);
        std::size_t length = strnlen(start, static_cast<std::size_t>(strings - name));
        if (!fits(blob, dataStart + offset, size) || dataStart + offset < dataStart) return corrupt("truncated PFS0 data");
        entries.push_back(Pfs0Entry{std::string(start, length), dataStart + offset, size});
    }
    return entries;
}

Result<std::vector<NamedFile>> extractPfs0(const Bytes& blob) {
    auto entries = readPfs0(blob);
    if (!entries) return entries.error();
    std::vector<NamedFile> out;
    for (const auto& e : entries.value()) {
        auto begin = blob.begin() + static_cast<std::ptrdiff_t>(e.offset);
        out.push_back(NamedFile{e.name, Bytes(begin, begin + static_cast<std::ptrdiff_t>(e.size))});
    }
    return out;
}

// ---------------------------------------------------------------------------
// RomFS: 0x50-byte header of ten u64 {header size, dir hash table offset/
// size, dir meta table offset/size, file hash table offset/size, file meta
// table offset/size, file data offset}. Data at 0x200, files aligned to
// 0x10; tables after the data.
//   dir entry:  parent, sibling, first child dir, first child file, hash next, name size, name
//   file entry: parent, sibling, u64 data offset, u64 size, hash next, name size, name

namespace {

constexpr std::uint32_t kNone = 0xFFFFFFFF;
constexpr std::uint64_t kRomFsDataOffset = 0x200;

std::uint32_t romfsHash(std::uint32_t parent, const std::string& name) {
    std::uint32_t hash = parent ^ 123456789u;
    for (unsigned char c : name) {
        hash = (hash >> 5) | (hash << 27);
        hash ^= c;
    }
    return hash;
}

std::uint32_t bucketCount(std::uint32_t entries) {
    if (entries < 3) return 3;
    if (entries < 19) return entries | 1;
    std::uint32_t n = entries;
    auto divisible = [](std::uint32_t v) {
        for (std::uint32_t d : {2u, 3u, 5u, 7u, 11u, 13u, 17u}) {
            if (v % d == 0) return true;
        }
        return false;
    };
    while (divisible(n)) ++n;
    return n;
}

struct RomFsHeader {
    std::uint64_t dirHash, dirHashSize, dirMeta, dirMetaSize, fileHash, fileHashSize, fileMeta, fileMetaSize, data;
};

Result<RomFsHeader> readHeader(const Bytes& blob) {
    if (blob.size() < 0x50 || get64(blob, 0) != 0x50) return corrupt("not a RomFS");
    RomFsHeader h{get64(blob, 0x08), get64(blob, 0x10), get64(blob, 0x18), get64(blob, 0x20), get64(blob, 0x28),
                  get64(blob, 0x30), get64(blob, 0x38), get64(blob, 0x40), get64(blob, 0x48)};
    if (!fits(blob, h.dirHash, h.dirHashSize) || !fits(blob, h.dirMeta, h.dirMetaSize) ||
        !fits(blob, h.fileHash, h.fileHashSize) || !fits(blob, h.fileMeta, h.fileMetaSize) || h.data > blob.size() ||
        h.fileHashSize < 4 || h.dirMetaSize < 0x18) {
        return corrupt("truncated RomFS");
    }
    return h;
}

// One file entry of the file meta table.
struct FileEntry {
    std::uint32_t sibling = kNone, hashNext = kNone;
    std::uint64_t offset = 0, size = 0;
    std::string name;
};

std::optional<FileEntry> fileAt(const Bytes& blob, const RomFsHeader& h, std::uint32_t at) {
    if (at > h.fileMetaSize || h.fileMetaSize - at < 0x20) return std::nullopt;
    std::size_t base = static_cast<std::size_t>(h.fileMeta + at);
    FileEntry e;
    e.sibling = get32(blob, base + 4);
    e.offset = get64(blob, base + 8);
    e.size = get64(blob, base + 16);
    e.hashNext = get32(blob, base + 24);
    std::uint32_t nameSize = get32(blob, base + 28);
    if (nameSize > h.fileMetaSize - at - 0x20) return std::nullopt;
    e.name.assign(reinterpret_cast<const char*>(blob.data() + base + 0x20), nameSize);
    if (!fits(blob, h.data + e.offset, e.size)) return std::nullopt;
    return e;
}

Bytes fileData(const Bytes& blob, const RomFsHeader& h, const FileEntry& e) {
    auto begin = blob.begin() + static_cast<std::ptrdiff_t>(h.data + e.offset);
    return Bytes(begin, begin + static_cast<std::ptrdiff_t>(e.size));
}

}  // namespace

Bytes buildRomFs(std::vector<NamedFile> files) {
    std::sort(files.begin(), files.end(), [](const NamedFile& a, const NamedFile& b) { return a.name < b.name; });

    // Data area.
    std::vector<std::uint64_t> dataOffsets;
    std::uint64_t dataSize = 0;
    for (const auto& f : files) {
        dataSize = alignUp(dataSize, 0x10);
        dataOffsets.push_back(dataSize);
        dataSize += f.data.size();
    }

    // File entry offsets in the file meta table.
    std::vector<std::uint32_t> entryOffsets;
    std::uint32_t fileMetaSize = 0;
    for (const auto& f : files) {
        entryOffsets.push_back(fileMetaSize);
        fileMetaSize += static_cast<std::uint32_t>(0x20 + alignUp(f.name.size(), 4));
    }

    std::uint32_t dirBuckets = bucketCount(1);
    std::uint32_t fileBuckets = bucketCount(static_cast<std::uint32_t>(files.size()));
    std::uint64_t dirHash = alignUp(kRomFsDataOffset + dataSize, 4);
    std::uint64_t dirMeta = dirHash + dirBuckets * 4u;
    std::uint64_t fileHash = dirMeta + 0x18;
    std::uint64_t fileMeta = fileHash + fileBuckets * 4u;
    Bytes out(fileMeta + fileMetaSize, 0);

    put64(out, 0x00, 0x50);
    put64(out, 0x08, dirHash);
    put64(out, 0x10, dirBuckets * 4u);
    put64(out, 0x18, dirMeta);
    put64(out, 0x20, 0x18);
    put64(out, 0x28, fileHash);
    put64(out, 0x30, fileBuckets * 4u);
    put64(out, 0x38, fileMeta);
    put64(out, 0x40, fileMetaSize);
    put64(out, 0x48, kRomFsDataOffset);

    for (std::size_t i = 0; i < files.size(); ++i) {
        putBytes(out, kRomFsDataOffset + dataOffsets[i], files[i].data.data(), files[i].data.size());
    }

    // Root directory: its own parent, no sibling or sub-directory.
    for (std::uint32_t b = 0; b < dirBuckets; ++b) put32(out, dirHash + 4 * b, kNone);
    put32(out, dirHash + 4 * (romfsHash(0, "") % dirBuckets), 0);
    put32(out, dirMeta + 0x00, 0);
    put32(out, dirMeta + 0x04, kNone);
    put32(out, dirMeta + 0x08, kNone);
    put32(out, dirMeta + 0x0C, files.empty() ? kNone : 0);
    put32(out, dirMeta + 0x10, kNone);
    put32(out, dirMeta + 0x14, 0);

    for (std::uint32_t b = 0; b < fileBuckets; ++b) put32(out, fileHash + 4 * b, kNone);
    for (std::size_t i = 0; i < files.size(); ++i) {
        std::size_t base = static_cast<std::size_t>(fileMeta + entryOffsets[i]);
        std::size_t bucket = static_cast<std::size_t>(fileHash + 4 * (romfsHash(0, files[i].name) % fileBuckets));
        put32(out, base + 0, 0);  // parent: the root
        put32(out, base + 4, i + 1 < files.size() ? entryOffsets[i + 1] : kNone);
        put64(out, base + 8, dataOffsets[i]);
        put64(out, base + 16, files[i].data.size());
        put32(out, base + 24, get32(out, bucket));  // chain the bucket's previous head
        put32(out, base + 28, static_cast<std::uint32_t>(files[i].name.size()));
        putString(out, base + 0x20, files[i].name, files[i].name.size());
        put32(out, bucket, entryOffsets[i]);
    }
    return out;
}

Result<std::vector<NamedFile>> readRomFs(const Bytes& blob) {
    auto header = readHeader(blob);
    if (!header) return header.error();
    const RomFsHeader& h = header.value();
    std::vector<NamedFile> out;
    std::uint32_t at = get32(blob, static_cast<std::size_t>(h.dirMeta + 0x0C));
    while (at != kNone) {
        auto entry = fileAt(blob, h, at);
        if (!entry || out.size() > h.fileMetaSize / 0x20) return corrupt("bad RomFS file entry");
        out.push_back(NamedFile{entry->name, fileData(blob, h, *entry)});
        at = entry->sibling;
    }
    return out;
}

std::optional<Bytes> findRomFsFile(const Bytes& blob, const std::string& name) {
    auto header = readHeader(blob);
    if (!header) return std::nullopt;
    const RomFsHeader& h = header.value();
    std::uint64_t buckets = h.fileHashSize / 4;
    std::uint32_t at = get32(blob, static_cast<std::size_t>(h.fileHash + 4 * (romfsHash(0, name) % buckets)));
    for (std::size_t guard = 0; at != kNone && guard <= h.fileMetaSize / 0x20; ++guard) {
        auto entry = fileAt(blob, h, at);
        if (!entry) return std::nullopt;
        if (entry->name == name) return fileData(blob, h, *entry);
        at = entry->hashNext;
    }
    return std::nullopt;
}

// ---------------------------------------------------------------------------
// Hash trees.

namespace {

void putDigest(Bytes& b, std::size_t at, const crypto::Sha256Digest& d) { putBytes(b, at, d.data(), d.size()); }

// SHA-256 of each `blockSize` block of data[offset, offset + size); the
// last block is zero-padded to the full size when `padLast`.
Bytes hashBlocks(const Bytes& data, std::uint64_t offset, std::uint64_t size, std::uint32_t blockSize, bool padLast) {
    Bytes table;
    Bytes padded;
    for (std::uint64_t at = 0; at < size; at += blockSize) {
        std::uint64_t n = std::min<std::uint64_t>(blockSize, size - at);
        crypto::Sha256Digest d;
        if (n < blockSize && padLast) {
            padded.assign(blockSize, 0);
            std::memcpy(padded.data(), data.data() + offset + at, static_cast<std::size_t>(n));
            d = crypto::Sha256::of(padded);
        } else {
            d = crypto::Sha256::of(data.data() + offset + at, static_cast<std::size_t>(n));
        }
        table.insert(table.end(), d.begin(), d.end());
    }
    return table;
}

constexpr int kIvfcLevels = 6;  // 5 hash levels + data
constexpr std::uint32_t kIvfcBlockLog2 = 14;
constexpr std::uint32_t kIvfcBlock = 1u << kIvfcBlockLog2;

}  // namespace

HashedSection buildSha256Section(const Bytes& pfs0, std::uint32_t blockSize) {
    Bytes table = hashBlocks(pfs0, 0, pfs0.size(), blockSize, false);
    std::uint64_t pfsOffset = alignUp(table.size(), 0x200);
    HashedSection out;
    out.data.assign(alignUp(pfsOffset + pfs0.size(), 0x200), 0);
    putBytes(out.data, 0, table.data(), table.size());
    putBytes(out.data, pfsOffset, pfs0.data(), pfs0.size());

    out.hashData.assign(0xF8, 0);
    putDigest(out.hashData, 0, crypto::Sha256::of(table));
    put32(out.hashData, 0x20, blockSize);
    put32(out.hashData, 0x24, 2);
    put64(out.hashData, 0x28, 0);
    put64(out.hashData, 0x30, table.size());
    put64(out.hashData, 0x38, pfsOffset);
    put64(out.hashData, 0x40, pfs0.size());
    return out;
}

Result<Bytes> verifySha256Section(const Bytes& data, const Bytes& hashData) {
    if (hashData.size() < 0x48) return corrupt("short hash data");
    std::uint32_t blockSize = get32(hashData, 0x20);
    std::uint64_t tableOffset = get64(hashData, 0x28), tableSize = get64(hashData, 0x30);
    std::uint64_t pfsOffset = get64(hashData, 0x38), pfsSize = get64(hashData, 0x40);
    if (blockSize == 0 || get32(hashData, 0x24) != 2 || !fits(data, tableOffset, tableSize) || !fits(data, pfsOffset, pfsSize)) {
        return corrupt("bad HierarchicalSha256 header");
    }
    auto master = crypto::Sha256::of(data.data() + tableOffset, static_cast<std::size_t>(tableSize));
    if (std::memcmp(master.data(), hashData.data(), 32) != 0) return mismatch("master hash mismatch");
    Bytes table = hashBlocks(data, pfsOffset, pfsSize, blockSize, false);
    if (table.size() != tableSize || std::memcmp(table.data(), data.data() + tableOffset, table.size()) != 0) {
        return mismatch("PFS0 block hash mismatch");
    }
    auto begin = data.begin() + static_cast<std::ptrdiff_t>(pfsOffset);
    return Bytes(begin, begin + static_cast<std::ptrdiff_t>(pfsSize));
}

HashedSection buildIvfcSection(const Bytes& romfs) {
    // Sizes from the data up: level i holds one hash per block of level i+1.
    std::uint64_t sizes[kIvfcLevels];
    sizes[kIvfcLevels - 1] = romfs.size();
    for (int i = kIvfcLevels - 2; i >= 0; --i) sizes[i] = (sizes[i + 1] + kIvfcBlock - 1) / kIvfcBlock * 32;
    std::uint64_t offsets[kIvfcLevels];
    std::uint64_t cursor = 0;
    for (int i = 0; i < kIvfcLevels; ++i) {
        offsets[i] = cursor;
        cursor = alignUp(cursor + sizes[i], kIvfcBlock);
    }

    HashedSection out;
    out.data.assign(alignUp(offsets[kIvfcLevels - 1] + sizes[kIvfcLevels - 1], 0x200), 0);
    putBytes(out.data, offsets[kIvfcLevels - 1], romfs.data(), romfs.size());
    for (int i = kIvfcLevels - 2; i >= 0; --i) {
        Bytes level = hashBlocks(out.data, offsets[i + 1], sizes[i + 1], kIvfcBlock, true);
        putBytes(out.data, offsets[i], level.data(), level.size());
    }

    out.hashData.assign(0xF8, 0);
    putBytes(out.hashData, 0, "IVFC", 4);
    put32(out.hashData, 0x04, 0x20000);
    put32(out.hashData, 0x08, 0x20);
    put32(out.hashData, 0x0C, kIvfcLevels + 1);  // + the master hash
    for (int i = 0; i < kIvfcLevels; ++i) {
        std::size_t info = 0x10 + 0x18 * static_cast<std::size_t>(i);
        put64(out.hashData, info, offsets[i]);
        put64(out.hashData, info + 8, sizes[i]);
        put32(out.hashData, info + 16, kIvfcBlockLog2);
    }
    Bytes master = hashBlocks(out.data, offsets[0], sizes[0], kIvfcBlock, true);
    putBytes(out.hashData, 0xC0, master.data(), 32);
    return out;
}

Result<Bytes> verifyIvfcSection(const Bytes& data, const Bytes& hashData) {
    if (hashData.size() < 0xE0 || std::memcmp(hashData.data(), "IVFC", 4) != 0 || get32(hashData, 0x0C) != kIvfcLevels + 1) {
        return corrupt("bad IVFC header");
    }
    std::uint64_t offsets[kIvfcLevels], sizes[kIvfcLevels];
    std::uint32_t blocks[kIvfcLevels];
    for (int i = 0; i < kIvfcLevels; ++i) {
        std::size_t info = 0x10 + 0x18 * static_cast<std::size_t>(i);
        offsets[i] = get64(hashData, info);
        sizes[i] = get64(hashData, info + 8);
        std::uint32_t log2 = get32(hashData, info + 16);
        if (log2 > 24 || !fits(data, offsets[i], sizes[i])) return corrupt("bad IVFC level");
        blocks[i] = 1u << log2;
    }
    Bytes master = hashBlocks(data, offsets[0], sizes[0], blocks[0], true);
    if (master.size() != 32 || std::memcmp(master.data(), hashData.data() + 0xC0, 32) != 0) return mismatch("IVFC master hash mismatch");
    for (int i = 0; i + 1 < kIvfcLevels; ++i) {
        Bytes level = hashBlocks(data, offsets[i + 1], sizes[i + 1], blocks[i + 1], true);
        if (level.size() > sizes[i] || std::memcmp(level.data(), data.data() + offsets[i], level.size()) != 0) {
            return mismatch("IVFC level " + std::to_string(i + 2) + " hash mismatch");
        }
    }
    auto begin = data.begin() + static_cast<std::ptrdiff_t>(offsets[kIvfcLevels - 1]);
    return Bytes(begin, begin + static_cast<std::ptrdiff_t>(sizes[kIvfcLevels - 1]));
}

}  // namespace rm::nsp
