#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "retromanager/core/Cancellation.hpp"
#include "retromanager/core/EventBus.hpp"
#include "retromanager/core/ITaskRunner.hpp"
#include "retromanager/models/Bios.hpp"
#include "retromanager/network/IRemoteSource.hpp"
#include "retromanager/platform/IFileSystem.hpp"
#include "retromanager/platform/SdLayout.hpp"

namespace rm {

enum class BiosState {
    Missing,
    Ok,            // present, digest matches the reference
    Unrecognized,  // present, another digest (other revision, bad dump): may or may not work
    Unverified,    // present, no reference digest to compare with
};

// One line of the "BIOS check" screen.
struct BiosStatus {
    std::string system;
    std::string fileName;
    std::string description;
    bool required = false;
    BiosState state = BiosState::Missing;
    std::string path;                // where it is (actual case) or would be installed
    std::optional<BiosEntry> offer;  // the shop can provide it
};

// Events (delivered on the main thread).
struct BiosInstalled {
    std::string fileName;
    Status result;
};

struct BiosInstallFinished {
    int installed = 0;
    int failed = 0;
};

// Checks and installs the BIOS files RetroArch cores look for in the
// system folder (RetroArch's system_directory when absolute,
// /retroarch/system otherwise).
//
// Downloads are streamed into a hidden staging file, verified, then renamed
// (IFileSystem::openWrite): a cancelled or corrupted transfer never leaves a
// half-written BIOS for a core to choke on.
class BiosManager {
  public:
    BiosManager(IFileSystem& fs, SdLayout layout, IRemoteSource& source, EventBus& bus,
                std::unique_ptr<ITaskRunner> background, std::vector<BiosFile> catalogue = bios::catalogue());
    ~BiosManager();

    BiosManager(const BiosManager&) = delete;
    BiosManager& operator=(const BiosManager&) = delete;

    std::string systemDirectory() const;

    // Every catalogue file, plus the files the shop offers that the
    // catalogue does not know (neogeo.zip...), sorted by system. Reads and
    // hashes the files present (a few MiB at most).
    std::vector<BiosStatus> check(const std::vector<BiosEntry>& offers);

    // Blocking. Verification: the shop's MD5 when it gives one (mismatch =
    // IntegrityError, nothing installed); otherwise the file is installed
    // and check() reports whether it matches the catalogue.
    Status installNow(const BiosEntry& offer, const CancellationToken& cancel);

    // installNow() for each entry, in the background: BiosInstalled after
    // each file, BiosInstallFinished at the end. False if already running.
    bool startInstall(std::vector<BiosEntry> offers);
    void cancel();
    bool running() const { return running_.load(); }

  private:
    // Existing file of that name in any case (FAT is case-insensitive, the
    // desktop mock is not), or the canonical path.
    std::string locate(const std::string& directory, const std::string& fileName);

    IFileSystem& fs_;
    SdLayout layout_;
    IRemoteSource& source_;
    EventBus& bus_;
    std::vector<BiosFile> catalogue_;
    std::unique_ptr<ITaskRunner> background_;  // reset (joined) first in the destructor

    std::mutex mutex_;
    std::shared_ptr<CancellationToken> current_;
    std::atomic<bool> running_{false};
};

}  // namespace rm
