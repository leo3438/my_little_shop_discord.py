#pragma once

#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "retromanager/models/AppEntry.hpp"
#include "retromanager/network/IRemoteSource.hpp"
#include "retromanager/platform/IFileSystem.hpp"
#include "retromanager/platform/SdLayout.hpp"
#include "retromanager/services/DownloadQueueManager.hpp"

namespace rm {

enum class AppState {
    NotInstalled,
    Installed,        // the .nro is there (version up to date, or unknown)
    UpdateAvailable,  // installed by RetroManager with another version than the shop's
};

// The personal homebrew store: installs .nro files from the shop in the
// standard hbmenu layout, /switch/<folder>/<folder>.nro, with an optional
// icon: /switch/<folder>/<folder>.jpg (hbmenu shows it for a .nro without
// an embedded icon) and the same image as icon.jpg.
//
// Downloads go through DownloadQueueManager (job()): same streamed, staged and
// space-checked pipeline as ROMs, plus an NRO header check (an HTML error
// page or a truncated file never replaces a working homebrew). The version
// installed is recorded in /switch/RetroManager/apps.json to detect updates.
class AppManager {
  public:
    static constexpr std::size_t kNroHeaderBytes = 0x14;  // "NRO0" magic at 0x10
    static constexpr std::size_t kMaxIconBytes = 2 * 1024 * 1024;

    AppManager(IFileSystem& fs, SdLayout layout, IRemoteSource& source);

    Result<std::string> folderFor(const AppEntry& app) const;
    Result<std::string> nroPathFor(const AppEntry& app) const;
    Result<std::string> iconPathFor(const AppEntry& app) const;       // <folder>/<folder>.jpg
    Result<std::string> iconAliasPathFor(const AppEntry& app) const;  // <folder>/icon.jpg
    std::string recordPath() const { return layout_.appDataDir + "/apps.json"; }

    AppState state(const AppEntry& app);
    // One read of the record file for a whole list, keyed by AppEntry::id.
    std::map<std::string, AppState> states(const std::vector<AppEntry>& apps);
    // Version recorded when RetroManager installed it; nullopt if unknown.
    std::optional<std::string> installedVersion(const AppEntry& app);

    // The download of `app`, to hand to DownloadQueueManager::start(). Its
    // follow-up: the icon ("icon" step), then the version record ("record"
    // step, reported only if it fails).
    DownloadJob job(const AppEntry& app);

    static Status validateNroHeader(std::string_view header);
    // JPEG only: hbmenu does not decode other formats. IntegrityError when
    // not an image at all.
    static Status validateIcon(const std::string& content);

  private:
    std::map<std::string, std::string> loadRecords();  // id -> version
    Status record(const AppEntry& app, const std::string& path);
    Status installIcon(const AppEntry& app, const CancellationToken& cancel);
    std::uint64_t remoteSize(const std::string& url);
    AppState stateWith(const AppEntry& app, const std::map<std::string, std::string>& records);

    IFileSystem& fs_;
    SdLayout layout_;
    IRemoteSource& source_;
};

}  // namespace rm
