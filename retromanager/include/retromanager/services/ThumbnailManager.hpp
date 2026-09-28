#pragma once

#include <cstddef>
#include <string>

#include "retromanager/network/IRemoteSource.hpp"
#include "retromanager/platform/IFileSystem.hpp"
#include "retromanager/platform/SdLayout.hpp"
#include "retromanager/services/PostInstallStep.hpp"

namespace rm {

// Installs the box art announced by a game's "boxart" URL where RetroArch
// looks for it when showing the playlist entry:
// <thumbnails dir>/<libretro system name>/Named_Boxarts/<label>.png, the
// label being the ROM file name without extension (as PlaylistManager
// writes it), with RetroArch's &*/:`<>?\| -> '_' substitution.
//
// Only PNG is accepted: RetroArch does not load other formats as
// thumbnails, and this also rejects HTML error pages.
class ThumbnailManager : public IPostInstallStep {
  public:
    static constexpr std::size_t kMaxImageBytes = 8 * 1024 * 1024;

    ThumbnailManager(IFileSystem& fs, SdLayout layout, IRemoteSource& source);

    std::string id() const override { return "boxart"; }

    // nullopt when the game has no box art.
    std::optional<Status> run(const GameEntry& game, const std::string& romPath,
                              const CancellationToken& cancel) override;

    // RetroArch's thumbnails_directory when absolute, /retroarch/thumbnails otherwise.
    std::string thumbnailsDirectory() const;
    Result<std::string> destinationFor(const GameEntry& game, const std::string& romPath) const;

    // IntegrityError when not an image, Unsupported for a non-PNG image.
    static Status validate(const std::string& content);

  private:
    IFileSystem& fs_;
    SdLayout layout_;
    IRemoteSource& source_;
};

}  // namespace rm
