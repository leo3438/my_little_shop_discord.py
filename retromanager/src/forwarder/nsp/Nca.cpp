#include "retromanager/forwarder/nsp/Nca.hpp"

#include <algorithm>
#include <cstring>

#include "Le.hpp"

namespace rm::nsp {

using namespace le;

// Header (0xC00, AES-XTS with header_key, 0x200-byte sectors from 0):
//   0x000 fixed-key signature, 0x100 NPDM signature (left blank: the
//         console needs signature patches for any self-made NCA)
//   0x200 "NCA3", 0x204 distribution (0 = download), 0x205 content type,
//   0x206 old key generation, 0x207 key area key index (0 = application),
//   0x208 content size, 0x210 program id, 0x218 content index,
//   0x21C SDK version, 0x220 key generation, 0x230 rights id (none)
//   0x240 4 section entries {start, end in 0x200 units, 1, 0}
//   0x280 SHA-256 of each FS header, 0x300 key area (AES-ECB with the key
//         area key; slot 2 = the AES-CTR key), 0x400 4 FS headers of 0x200.
// FS header: version 2, FS type (0 RomFS, 1 PFS0), hash type (2
// HierarchicalSha256, 3 IVFC), encryption 3 (CTR), hash data at 0x8, CTR
// upper IV at 0x140.

namespace {

constexpr std::size_t kHeaderSize = 0xC00;
constexpr std::size_t kMediaUnit = 0x200;

Error corrupt(const std::string& what) { return Error{ErrorCode::ParseError, what}; }

std::uint64_t upperIvFor(std::size_t section) { return static_cast<std::uint64_t>(section + 1) << 32; }

}  // namespace

Bytes buildNca(const NcaSpec& spec, const NcaKeys& keys) {
    Bytes header(kHeaderSize, 0);
    Bytes body;
    std::uint64_t offset = kHeaderSize;
    for (std::size_t i = 0; i < spec.sections.size() && i < 4; ++i) {
        const NcaSection& section = spec.sections[i];
        bool pfs = section.kind == NcaSection::Kind::Pfs0;
        HashedSection hashed = pfs ? buildSha256Section(section.payload) : buildIvfcSection(section.payload);

        Bytes fs(0x200, 0);
        put16(fs, 0x00, 2);
        fs[0x02] = pfs ? 1 : 0;
        fs[0x03] = pfs ? 2 : 3;
        fs[0x04] = 3;
        putBytes(fs, 0x08, hashed.hashData.data(), hashed.hashData.size());
        std::uint64_t upperIv = upperIvFor(i);
        put64(fs, 0x140, upperIv);

        crypto::AesBlock counter = crypto::ncaCounter(upperIv, offset);
        crypto::ctrTransform(spec.contentKey, counter, hashed.data.data(), hashed.data.size());

        std::size_t entry = 0x240 + 0x10 * i;
        put32(header, entry, static_cast<std::uint32_t>(offset / kMediaUnit));
        put32(header, entry + 4, static_cast<std::uint32_t>((offset + hashed.data.size()) / kMediaUnit));
        put32(header, entry + 8, 1);
        auto fsHash = crypto::Sha256::of(fs);
        putBytes(header, 0x280 + 0x20 * i, fsHash.data(), fsHash.size());
        putBytes(header, 0x400 + 0x200 * i, fs.data(), fs.size());

        body.insert(body.end(), hashed.data.begin(), hashed.data.end());
        offset += hashed.data.size();
    }

    putBytes(header, 0x200, "NCA3", 4);
    header[0x204] = 0;
    header[0x205] = static_cast<std::uint8_t>(spec.type);
    header[0x206] = 0;
    header[0x207] = 0;
    put64(header, 0x208, offset);
    put64(header, 0x210, spec.titleId);
    put32(header, 0x21C, spec.sdkVersion);
    header[0x220] = 0;

    Bytes keyArea(0x40, 0);
    putBytes(keyArea, 0x20, spec.contentKey.data(), 16);
    crypto::ecbEncrypt(keys.keyAreaKey, keyArea.data(), keyArea.size());
    putBytes(header, 0x300, keyArea.data(), keyArea.size());

    crypto::xtsEncrypt(keys.headerDataKey, keys.headerTweakKey, header.data(), header.size(), kMediaUnit, 0);
    header.insert(header.end(), body.begin(), body.end());
    return header;
}

Result<NcaContents> readNca(const Bytes& nca, const NcaKeys& keys) {
    if (nca.size() < kHeaderSize) return corrupt("truncated NCA");
    Bytes header(nca.begin(), nca.begin() + kHeaderSize);
    crypto::xtsDecrypt(keys.headerDataKey, keys.headerTweakKey, header.data(), header.size(), kMediaUnit, 0);
    if (std::memcmp(header.data() + 0x200, "NCA3", 4) != 0) return corrupt("not an NCA3 (wrong header_key?)");
    if (header[0x207] != 0 || header[0x206] != 0 || header[0x220] != 0) return corrupt("unexpected key generation");
    for (std::size_t i = 0x230; i < 0x240; ++i) {
        if (header[i] != 0) return corrupt("titlekey crypto is not supported");
    }

    NcaContents out;
    out.type = static_cast<ContentType>(header[0x205]);
    out.contentSize = get64(header, 0x208);
    out.titleId = get64(header, 0x210);
    if (out.contentSize != nca.size()) return corrupt("NCA size mismatch");

    Bytes keyArea(header.begin() + 0x300, header.begin() + 0x340);
    crypto::ecbDecrypt(keys.keyAreaKey, keyArea.data(), keyArea.size());
    crypto::AesKey contentKey{};
    std::copy_n(keyArea.begin() + 0x20, 16, contentKey.begin());

    for (std::size_t i = 0; i < 4; ++i) {
        std::size_t entry = 0x240 + 0x10 * i;
        std::uint64_t start = static_cast<std::uint64_t>(get32(header, entry)) * kMediaUnit;
        std::uint64_t end = static_cast<std::uint64_t>(get32(header, entry + 4)) * kMediaUnit;
        if (start == 0 && end == 0) continue;
        if (start < kHeaderSize || end <= start || end > nca.size()) return corrupt("bad NCA section bounds");

        Bytes fs(header.begin() + static_cast<std::ptrdiff_t>(0x400 + 0x200 * i),
                 header.begin() + static_cast<std::ptrdiff_t>(0x600 + 0x200 * i));
        auto fsHash = crypto::Sha256::of(fs);
        if (std::memcmp(fsHash.data(), header.data() + 0x280 + 0x20 * i, 32) != 0) {
            return Error{ErrorCode::IntegrityError, "FS header hash mismatch"};
        }
        if (get16(fs, 0) != 2 || fs[0x04] != 3) return corrupt("unsupported FS header");

        Bytes data(nca.begin() + static_cast<std::ptrdiff_t>(start), nca.begin() + static_cast<std::ptrdiff_t>(end));
        crypto::AesBlock counter = crypto::ncaCounter(get64(fs, 0x140), start);
        crypto::ctrTransform(contentKey, counter, data.data(), data.size());
        Bytes hashData(fs.begin() + 0x08, fs.begin() + 0x100);

        NcaSection section;
        Result<Bytes> payload = Error{ErrorCode::ParseError, "unknown FS type"};
        if (fs[0x02] == 1 && fs[0x03] == 2) {
            section.kind = NcaSection::Kind::Pfs0;
            payload = verifySha256Section(data, hashData);
        } else if (fs[0x02] == 0 && fs[0x03] == 3) {
            section.kind = NcaSection::Kind::RomFs;
            payload = verifyIvfcSection(data, hashData);
        }
        if (!payload) return payload.error();
        section.payload = std::move(payload).value();
        out.sections.push_back(std::move(section));
    }
    return out;
}

// ---------------------------------------------------------------------------

namespace {

ContentRecord recordOf(const Bytes& nca, CnmtContentType type) {
    ContentRecord r;
    r.hash = crypto::Sha256::of(nca);
    r.size = nca.size();
    r.type = type;
    return r;
}

}  // namespace

Result<Bytes> buildApplicationNsp(const ApplicationSpec& spec, const NcaKeys& keys, const ContentKeys& contentKeys) {
    if (spec.romfs.empty()) return Error{ErrorCode::InvalidArgument, "the program RomFS is empty"};
    if (spec.nacp.size() != kNacpSize) return Error{ErrorCode::InvalidArgument, "bad control.nacp"};

    std::vector<NamedFile> exefs = spec.exefs;
    bool hasMain = false, hasNpdm = false;
    for (auto& f : exefs) {
        if (f.name == "main") hasMain = true;
        if (f.name == "main.npdm") {
            auto patched = patchNpdmTitleId(f.data, spec.titleId);
            if (!patched) return patched.error();
            f.data = std::move(patched).value();
            hasNpdm = true;
        }
    }
    if (!hasMain || !hasNpdm) return Error{ErrorCode::NotFound, "the ExeFS needs main and main.npdm"};

    NcaSpec program;
    program.type = ContentType::Program;
    program.titleId = spec.titleId;
    program.contentKey = contentKeys.program;
    program.sections.push_back({NcaSection::Kind::Pfs0, buildPfs0(exefs)});
    program.sections.push_back({NcaSection::Kind::RomFs, buildRomFs(spec.romfs)});
    if (!spec.logo.empty()) program.sections.push_back({NcaSection::Kind::Pfs0, buildPfs0(spec.logo)});
    Bytes programNca = buildNca(program, keys);

    std::vector<NamedFile> controlFiles = {{"control.nacp", spec.nacp}};
    for (const char* language : nacpLanguageNames()) controlFiles.push_back({std::string("icon_") + language + ".dat", spec.icon});
    NcaSpec control;
    control.type = ContentType::Control;
    control.titleId = spec.titleId;
    control.contentKey = contentKeys.control;
    control.sections.push_back({NcaSection::Kind::RomFs, buildRomFs(controlFiles)});
    Bytes controlNca = buildNca(control, keys);

    ContentRecord programRecord = recordOf(programNca, CnmtContentType::Program);
    ContentRecord controlRecord = recordOf(controlNca, CnmtContentType::Control);
    Bytes cnmt = buildApplicationCnmt(spec.titleId, spec.version, {programRecord, controlRecord});
    NcaSpec meta;
    meta.type = ContentType::Meta;
    meta.titleId = spec.titleId;
    meta.contentKey = contentKeys.meta;
    meta.sections.push_back({NcaSection::Kind::Pfs0, buildPfs0({{cnmtFileName(spec.titleId), cnmt}})});
    Bytes metaNca = buildNca(meta, keys);
    ContentRecord metaRecord = recordOf(metaNca, CnmtContentType::Meta);

    return buildPfs0({{programRecord.contentId() + ".nca", std::move(programNca)},
                      {controlRecord.contentId() + ".nca", std::move(controlNca)},
                      {metaRecord.contentId() + ".cnmt.nca", std::move(metaNca)}});
}

Result<ApplicationContents> readApplicationNsp(const Bytes& nsp, const NcaKeys& keys) {
    auto files = extractPfs0(nsp);
    if (!files) return files.error();
    auto integrity = [](const std::string& what) { return Error{ErrorCode::IntegrityError, what}; };

    ApplicationContents out;
    const NamedFile* metaFile = nullptr;
    for (const auto& f : files.value()) {
        out.fileNames.push_back(f.name);
        auto hash = crypto::Sha256::of(f.data);
        std::string id = crypto::toHex(hash.data(), 16);
        bool isMeta = f.name == id + ".cnmt.nca";
        if (!isMeta && f.name != id + ".nca") return integrity(f.name + ": name is not its content id");
        if (isMeta) metaFile = &f;
    }
    if (metaFile == nullptr) return corrupt("no .cnmt.nca in the NSP");

    auto meta = readNca(metaFile->data, keys);
    if (!meta) return meta.error();
    if (meta.value().type != ContentType::Meta || meta.value().sections.empty()) return corrupt("bad meta NCA");
    auto metaPfs = extractPfs0(meta.value().sections[0].payload);
    if (!metaPfs || metaPfs.value().empty()) return corrupt("bad meta PFS0");
    auto cnmt = readCnmt(metaPfs.value()[0].data);
    if (!cnmt) return cnmt.error();
    out.cnmt = cnmt.value();
    out.titleId = out.cnmt.titleId;
    if (metaPfs.value()[0].name != cnmtFileName(out.titleId) || meta.value().titleId != out.titleId) {
        return integrity("meta NCA title id mismatch");
    }

    for (const auto& record : out.cnmt.contents) {
        const NamedFile* file = nullptr;
        for (const auto& f : files.value()) {
            if (f.name == record.contentId() + ".nca") file = &f;
        }
        if (file == nullptr) return integrity("content " + record.contentId() + " missing from the NSP");
        if (crypto::Sha256::of(file->data) != record.hash || file->data.size() != record.size) {
            return integrity("content " + record.contentId() + " does not match the CNMT");
        }
        auto nca = readNca(file->data, keys);
        if (!nca) return nca.error();
        if (nca.value().titleId != out.titleId) return integrity("NCA title id mismatch");
        if (record.type == CnmtContentType::Program) {
            if (nca.value().type != ContentType::Program || nca.value().sections.size() < 2) return corrupt("bad program NCA");
            auto exefs = extractPfs0(nca.value().sections[0].payload);
            auto romfs = readRomFs(nca.value().sections[1].payload);
            if (!exefs) return exefs.error();
            if (!romfs) return romfs.error();
            out.exefs = exefs.value();
            out.romfs = romfs.value();
        } else if (record.type == CnmtContentType::Control) {
            if (nca.value().type != ContentType::Control || nca.value().sections.empty()) return corrupt("bad control NCA");
            auto romfs = readRomFs(nca.value().sections[0].payload);
            if (!romfs) return romfs.error();
            out.control = romfs.value();
        }
    }
    return out;
}

}  // namespace rm::nsp
