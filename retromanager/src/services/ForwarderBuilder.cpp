#include "retromanager/services/ForwarderBuilder.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <map>
#include <random>

#include "retromanager/core/FileName.hpp"
#include "retromanager/core/Format.hpp"
#include "retromanager/forwarder/IconMaker.hpp"
#include "retromanager/fs/RomStore.hpp"
#include "retromanager/models/Systems.hpp"

namespace rm {

namespace {

Bytes toBytes(const std::string& s) { return Bytes(s.begin(), s.end()); }

ForwarderReport issue(ForwarderIssue kind, std::string detail) {
    ForwarderReport r;
    r.issue = kind;
    r.detail = std::move(detail);
    return r;
}

// A fresh content key. It protects nothing secret (the key area wraps it
// with the console's key), it only has to differ between NCAs; so when the
// platform has no random_device (some newlib builds throw), the clock and a
// counter hashed together do.
crypto::AesKey randomKey() {
    static std::atomic<std::uint64_t> counter{0};
    std::uint64_t seed[4] = {static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count()),
                             static_cast<std::uint64_t>(std::chrono::system_clock::now().time_since_epoch().count()),
                             counter.fetch_add(1), 0};
    try {
        std::random_device device;
        seed[3] = (static_cast<std::uint64_t>(device()) << 32) | device();
    } catch (...) {
    }
    auto digest = crypto::Sha256::of(seed, sizeof seed);
    crypto::AesKey key{};
    std::copy_n(digest.begin(), key.size(), key.begin());
    return key;
}

// A colour per system for the placeholder icon (stable, not too dark).
forwarder::Rgb systemColour(const std::string& system) {
    auto d = crypto::Sha256::of(system.data(), system.size());
    return forwarder::Rgb{static_cast<std::uint8_t>(64 + d[0] % 160), static_cast<std::uint8_t>(64 + d[1] % 160),
                          static_cast<std::uint8_t>(64 + d[2] % 160)};
}

struct Stub {
    std::vector<nsp::NamedFile> exefs;
    std::vector<nsp::NamedFile> logo;
};

}  // namespace

ForwarderBuilder::ForwarderBuilder(IFileSystem& fs, SdLayout layout, BoxArtLocator boxArt, Packager packager)
    : fs_(fs), layout_(std::move(layout)), boxArt_(std::move(boxArt)), packager_(packager ? std::move(packager) : defaultPackager()) {}

ForwarderBuilder::Packager ForwarderBuilder::defaultPackager() {
    return [](const nsp::ApplicationSpec& spec, const nsp::NcaKeys& keys) -> Result<Bytes> {
        nsp::ContentKeys contentKeys{randomKey(), randomKey(), randomKey()};
        auto nsp = nsp::buildApplicationNsp(spec, keys, contentKeys);
        if (!nsp) return nsp.error();
        // Read everything back before it reaches the card: a broken NSP
        // would only show up at install time, on the console.
        auto check = nsp::readApplicationNsp(nsp.value(), keys);
        if (!check) return Error{check.error().code, "self-check of the generated NSP failed: " + check.error().message};
        return nsp;
    };
}

std::vector<std::string> ForwarderBuilder::coreCandidates(const std::string& system) {
    // RetroArch for Switch cores (<name>_libretro_libnx.nro), best first.
    static const std::map<std::string, std::vector<std::string>> cores = {
        {"nes", {"fceumm", "nestopia", "mesen"}},
        {"snes", {"snes9x", "snes9x2010", "bsnes"}},
        {"n64", {"mupen64plus_next", "parallel_n64"}},
        {"gb", {"gambatte", "sameboy", "mgba"}},
        {"gbc", {"gambatte", "sameboy", "mgba"}},
        {"gba", {"mgba", "vba_next", "gpsp"}},
        {"nds", {"desmume", "melonds"}},
        {"mastersystem", {"genesis_plus_gx", "picodrive"}},
        {"megadrive", {"genesis_plus_gx", "picodrive"}},
        {"gamegear", {"genesis_plus_gx"}},
        {"pcengine", {"mednafen_pce_fast"}},
        {"psx", {"pcsx_rearmed", "swanstation"}},
        {"arcade", {"fbneo", "mame2003_plus"}},
    };
    auto it = cores.find(system);
    return it == cores.end() ? std::vector<std::string>{} : it->second;
}

std::optional<std::string> ForwarderBuilder::installedCore(const std::string& system) {
    for (const auto& core : coreCandidates(system)) {
        std::string path = layout_.coresDir + "/" + core + "_libretro_libnx.nro";
        if (fs_.isFile(path)) return path;
    }
    return std::nullopt;
}

std::string ForwarderBuilder::outputPathFor(const GameEntry& game) const {
    // ASCII only: DBI and Tinfoil must list it, whatever the title (the NACP
    // keeps the real one, accents included, for the HOME menu).
    std::string name = asciiFileName(game.title);
    if (name.empty()) {
        std::string_view rom = game.fileName;
        name = asciiFileName(rom.substr(0, rom.rfind('.')));
    }
    if (name.empty()) {
        auto rom = RomStore(fs_, layout_).destinationFor(game);
        name = "Forwarder " + nsp::titleIdHex(nsp::forwarderTitleId(rom ? rom.value() : game.id));
    }
    return layout_.nspDir + "/" + name + ".nsp";
}

ForwarderReport ForwarderBuilder::checkPrerequisites() {
    if (!fs_.isFile(layout_.prodKeys)) return issue(ForwarderIssue::KeysMissing, layout_.prodKeys);
    ForwarderReport missing = issue(ForwarderIssue::StubMissing, layout_.forwarderStubDir);
    for (const char* name : {"main", "main.npdm"}) {
        if (!fs_.isFile(layout_.forwarderStubDir + "/" + name) && !fs_.isFile(layout_.forwarderStubDir + "/exefs/" + name)) {
            missing.missingFiles.push_back(name);
        }
    }
    if (!missing.missingFiles.empty()) return missing;
    return ForwarderReport{};
}

ForwarderReport ForwarderBuilder::build(const GameEntry& game, const CancellationToken& cancel) {
    ForwarderReport ready = checkPrerequisites();
    if (!ready.ok()) return ready;

    // Keys.
    auto keysText = fs_.readFile(layout_.prodKeys);
    if (!keysText) return issue(ForwarderIssue::KeysMissing, layout_.prodKeys);
    auto keys = nsp::parseProdKeys(keysText.value());
    if (!keys) {
        std::string name = keys.error().message.find(nsp::kHeaderKeyName) != std::string::npos ? nsp::kHeaderKeyName
                                                                                               : nsp::kKeyAreaKeyName;
        return issue(ForwarderIssue::KeysInvalid, name);
    }

    // Stub: stub/<file> or stub/exefs/<file>, plus an optional stub/logo/.
    Stub stub;
    for (const char* name : {"main", "main.npdm"}) {
        std::string path = layout_.forwarderStubDir + "/" + name;
        if (!fs_.isFile(path)) path = layout_.forwarderStubDir + "/exefs/" + name;
        auto data = fs_.readFile(path);
        if (!data) return issue(ForwarderIssue::Failed, data.error().describe());
        stub.exefs.push_back({name, toBytes(data.value())});
    }
    if (!nsp::npdmTitleId(stub.exefs[1].data)) return issue(ForwarderIssue::StubInvalid, layout_.forwarderStubDir + "/main.npdm");
    if (auto logo = fs_.listDirectory(layout_.forwarderStubDir + "/logo")) {
        for (const auto& entry : logo.value()) {
            if (entry.type != EntryType::File) continue;
            auto data = fs_.readFile(layout_.forwarderStubDir + "/logo/" + entry.name);
            if (data) stub.logo.push_back({entry.name, toBytes(data.value())});
        }
    }

    // Game and core.
    auto rom = RomStore(fs_, layout_).destinationFor(game);
    if (!rom || !fs_.isFile(rom.value())) return issue(ForwarderIssue::RomMissing, rom ? rom.value() : game.fileName);
    auto core = installedCore(game.system);
    if (!core) {
        ForwarderReport r = issue(ForwarderIssue::CoreMissing, layout_.coresDir);
        for (const auto& c : coreCandidates(game.system)) r.cores.push_back(c + "_libretro_libnx.nro");
        return r;
    }
    if (cancel.isCancelled()) return issue(ForwarderIssue::Cancelled, "");

    // Icon: the box art on the card, else a plain square.
    ForwarderReport report;
    Bytes icon;
    if (boxArt_) {
        if (auto path = boxArt_(game, rom.value())) {
            if (auto art = fs_.readFile(*path)) {
                if (auto made = forwarder::makeIcon(toBytes(art.value()))) icon = std::move(made).value();
            }
        }
    }
    if (icon.empty()) {
        icon = forwarder::placeholderIcon(systemColour(game.system));
        report.placeholderIcon = true;
    }

    // The application.
    const std::string title = game.title.empty() ? game.fileName : game.title;
    nsp::ApplicationSpec spec;
    spec.titleId = nsp::forwarderTitleId(rom.value());
    spec.exefs = std::move(stub.exefs);
    spec.logo = std::move(stub.logo);
    std::string nro = "sdmc:" + *core;
    spec.romfs = {{"nextNroPath", toBytes(nro)}, {"nextArgv", toBytes("\"" + nro + "\" \"sdmc:" + rom.value() + "\"")}};
    nsp::NacpSpec nacp;
    nacp.titleId = spec.titleId;
    nacp.name = title;
    nacp.publisher = "RetroArch - " + systems::displayName(game.system);
    spec.nacp = nsp::buildNacp(nacp);
    spec.icon = std::move(icon);

    auto package = packager_(spec, keys.value());
    if (!package) return issue(ForwarderIssue::Failed, package.error().describe());
    if (cancel.isCancelled()) return issue(ForwarderIssue::Cancelled, "");

    const std::string output = outputPathFor(game);
    Status dir = fs_.createDirectories(layout_.nspDir);
    if (!dir) return issue(ForwarderIssue::WriteFailed, dir.error().describe());
    const Bytes& bytes = package.value();
    Status written = fs_.writeFile(output, std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
    if (!written) return issue(ForwarderIssue::WriteFailed, written.error().describe());
    // Flush to the card itself before checking it (and before the user
    // pulls it out to copy the NSP).
    if (Status committed = fs_.commit(); !committed) report.warning = committed.error().describe();
    // Read the card back: a write can report success and still not land
    // (full or failing SD card), and the user would look for it in vain.
    auto landed = fs_.stat(output);
    if (!landed || landed.value().type != EntryType::File) {
        return issue(ForwarderIssue::WriteFailed, output + " is not on the SD card after writing it");
    }
    if (landed.value().size != bytes.size()) {
        return issue(ForwarderIssue::WriteFailed, output + ": " + formatBytes(landed.value().size) + " on the SD card instead of " +
                                                      formatBytes(bytes.size()));
    }

    report.nspPath = output;
    report.title = title;
    report.titleId = spec.titleId;
    report.corePath = *core;
    report.romPath = rom.value();
    report.sizeBytes = bytes.size();
    return report;
}

}  // namespace rm
