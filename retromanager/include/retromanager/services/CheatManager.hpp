#pragma once

#include <cstddef>
#include <string>

#include "retromanager/network/IRemoteSource.hpp"
#include "retromanager/platform/IFileSystem.hpp"
#include "retromanager/platform/SdLayout.hpp"
#include "retromanager/services/PostInstallStep.hpp"

namespace rm {

// Installs the RetroArch cheat file (.cht) announced by a game's cheat_url:
// <cheats dir>/<libretro system name>/<ROM name without extension>.cht,
// e.g. /retroarch/cheats/Nintendo - Nintendo DS/Pokemon Platine (France).cht
//
// The cheats directory is RetroArch's cheat_database_path when retroarch.cfg
// sets an absolute one, /retroarch/cheats otherwise.
class CheatManager : public IPostInstallStep {
  public:
    static constexpr std::size_t kMaxCheatBytes = 1024 * 1024;

    CheatManager(IFileSystem& fs, SdLayout layout, IRemoteSource& source);

    std::string id() const override { return "cheats"; }

    // nullopt when the game has no cheat_url.
    std::optional<Status> run(const GameEntry& game, const std::string& romPath,
                              const CancellationToken& cancel) override;

    Result<std::string> destinationFor(const GameEntry& game, const std::string& romPath) const;
    std::string cheatsDirectory() const;

    // A .cht must parse as a RetroArch cheat file ("cheats = N"): this
    // rejects HTML error pages and truncated downloads.
    static Status validate(const std::string& content);

  private:
    IFileSystem& fs_;
    SdLayout layout_;
    IRemoteSource& source_;
};

}  // namespace rm
