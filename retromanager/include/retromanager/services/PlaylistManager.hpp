#pragma once

#include <string>

#include "retromanager/platform/IFileSystem.hpp"
#include "retromanager/platform/SdLayout.hpp"
#include "retromanager/services/PostInstallStep.hpp"

namespace rm {

// Lists each installed game in the RetroArch playlist of its system, so it
// shows up in RetroArch's main menu with its thumbnails:
// <playlist dir>/<libretro system name>.lpl, e.g.
// /retroarch/playlists/Nintendo - Nintendo DS.lpl
//
// Entry: the ROM path, its file name without extension as label, "DETECT"
// as core (RetroArch uses the playlist default or asks), and the CRC-32
// measured during the download. The playlist is created if needed and
// merged otherwise (PlaylistDocument): RetroArch's own entries and settings
// are kept, a copy "<name>.lpl.rmbak" is made before the first change.
class PlaylistManager : public IPostInstallStep {
  public:
    PlaylistManager(IFileSystem& fs, SdLayout layout);

    std::string id() const override { return "playlist"; }

    // - RetroArch not installed: NotFound, nothing created.
    // - System without a libretro database name (arcade): Unsupported.
    // - Existing playlist that cannot be read: ParseError, file untouched.
    std::optional<Status> run(const GameEntry& game, const std::string& romPath,
                              const CancellationToken& cancel) override;

    // RetroArch's playlist_directory when absolute, /retroarch/playlists otherwise.
    std::string playlistsDirectory() const;
    Result<std::string> playlistPathFor(const GameEntry& game) const;

  private:
    IFileSystem& fs_;
    SdLayout layout_;
};

}  // namespace rm
