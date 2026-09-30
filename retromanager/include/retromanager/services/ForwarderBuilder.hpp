#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "retromanager/core/Cancellation.hpp"
#include "retromanager/core/Result.hpp"
#include "retromanager/forwarder/nsp/Nca.hpp"
#include "retromanager/models/GameEntry.hpp"
#include "retromanager/platform/IFileSystem.hpp"
#include "retromanager/platform/SdLayout.hpp"

namespace rm {

// Why a forwarder could not be made: each one maps to a message telling the
// user exactly what to do (UI), `detail` carrying the path or name involved.
enum class ForwarderIssue {
    None,
    KeysMissing,   // no prod.keys at detail
    KeysInvalid,   // prod.keys lacks or garbles a key (detail = the key name)
    StubMissing,   // detail = the stub folder; missingFiles = what is absent
    StubInvalid,   // main.npdm is not an NPDM
    RomMissing,    // the game is not installed (detail = expected ROM path)
    CoreMissing,   // no RetroArch core for the system (detail = cores folder)
    WriteFailed,   // /nsp could not be written (detail = the error)
    Cancelled,
    Failed,        // anything else (detail = the error)
};

struct ForwarderReport {
    ForwarderIssue issue = ForwarderIssue::None;
    std::string detail;
    std::vector<std::string> missingFiles;  // StubMissing
    std::vector<std::string> cores;         // CoreMissing: the cores that would do

    // Success
    std::string nspPath;      // virtual path, /nsp/<title>.nsp
    std::string title;
    std::uint64_t titleId = 0;
    std::string corePath;     // virtual path of the core the forwarder starts
    std::string romPath;
    bool placeholderIcon = false;  // no box art on the card: a plain icon was used
    std::string warning;           // not fatal (the NSP was read back fine), shown and logged
    std::uint64_t sizeBytes = 0;

    bool ok() const { return issue == ForwarderIssue::None; }
};

// Builds a HOME-menu shortcut (forwarder NSP) for an installed game: a tiny
// application that starts RetroArch's core with the ROM, through the
// forwarder stub (nx-hbloader style: it launches romfs:/nextNroPath with the
// arguments in romfs:/nextArgv).
//
// Inputs on the SD card:
//   /switch/prod.keys                    header_key + key_area_key_application_00
//   /switch/RetroManager/stub/main       the stub's ExeFS (a stub/exefs/
//   /switch/RetroManager/stub/main.npdm  subfolder is accepted too)
//   /switch/RetroManager/stub/logo/      optional NintendoLogo.png, StartupMovie.gif
// Output: /nsp/<title>.nsp, to be installed with DBI or Tinfoil (signature
// patches required: the NCAs cannot carry Nintendo's signatures).
class ForwarderBuilder {
  public:
    // Where RetroArch's box art for a game would be (ThumbnailManager).
    using BoxArtLocator = std::function<std::optional<std::string>(const GameEntry& game, const std::string& romPath)>;
    // Turns the application description into NSP bytes; the default one is
    // nsp::buildApplicationNsp with random content keys, read back and
    // verified before anything is written. Tests plug a recording mock.
    using Packager = std::function<Result<Bytes>(const nsp::ApplicationSpec& spec, const nsp::NcaKeys& keys)>;

    ForwarderBuilder(IFileSystem& fs, SdLayout layout, BoxArtLocator boxArt = nullptr, Packager packager = nullptr);

    static Packager defaultPackager();

    // RetroArch cores able to run `system`, preferred first ("mgba"...).
    static std::vector<std::string> coreCandidates(const std::string& system);
    // The first installed candidate, as a virtual path.
    std::optional<std::string> installedCore(const std::string& system);

    std::string outputPathFor(const GameEntry& game) const;

    // Keys and stub only: what the UI checks before offering the option.
    ForwarderReport checkPrerequisites();

    ForwarderReport build(const GameEntry& game, const CancellationToken& cancel);

  private:
    IFileSystem& fs_;
    SdLayout layout_;
    BoxArtLocator boxArt_;
    Packager packager_;
};

}  // namespace rm
