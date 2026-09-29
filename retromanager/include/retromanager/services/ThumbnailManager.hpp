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
//
// Scraper (optional): when the index gives no box art, or it cannot be
// fetched, the public libretro thumbnail server is tried:
// <scraper base>/<libretro system>/Named_Boxarts/<label>.png (the label
// percent-encoded). It works when ROMs carry their No-Intro names, as
// libretro's thumbnails do. A miss is silent: no box art is no error.
class ThumbnailManager : public IPostInstallStep {
  public:
    static constexpr std::size_t kMaxImageBytes = 8 * 1024 * 1024;

    // `scraperBaseUrl` empty = no fallback ("https://thumbnails.libretro.com/").
    ThumbnailManager(IFileSystem& fs, SdLayout layout, IRemoteSource& source, std::string scraperBaseUrl = "");

    std::string id() const override { return "boxart"; }

    // nullopt when there is nothing to report: no box art in the index and
    // none on the scraper (or no scraper).
    std::optional<Status> run(const GameEntry& game, const std::string& romPath,
                              const CancellationToken& cancel) override;

    // RetroArch's thumbnails_directory when absolute, /retroarch/thumbnails otherwise.
    std::string thumbnailsDirectory() const;
    Result<std::string> destinationFor(const GameEntry& game, const std::string& romPath) const;
    Result<std::string> scraperUrlFor(const GameEntry& game, const std::string& romPath) const;

    // IntegrityError when not an image, Unsupported for a non-PNG image.
    static Status validate(const std::string& content);

  private:
    Status install(const std::string& url, const std::string& destination, const CancellationToken& cancel);

    IFileSystem& fs_;
    SdLayout layout_;
    IRemoteSource& source_;
    std::string scraperBaseUrl_;
};

}  // namespace rm
