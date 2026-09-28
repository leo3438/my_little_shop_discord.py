#pragma once

#include <string>

#include "retromanager/platform/IFileSystem.hpp"
#include "retromanager/platform/SdLayout.hpp"
#include "retromanager/services/PostInstallStep.hpp"

namespace rm {

// Edits retroarch.cfg through CfgDocument, touching only the keys it owns.
//
// After an install it points RetroArch's file browser
// (rgui_browser_directory) at the ROM's folder, so "Load Content" opens
// directly on the new game (e.g. "/roms/nds/" after a DS download).
class EmulatorConfigurator : public IPostInstallStep {
  public:
    static constexpr const char* kBrowserDirectoryKey = "rgui_browser_directory";

    EmulatorConfigurator(IFileSystem& fs, SdLayout layout);

    std::string id() const override { return "retroarch"; }
    std::optional<Status> run(const GameEntry& game, const std::string& romPath,
                              const CancellationToken& cancel) override;

    // Sets rgui_browser_directory to `directory`.
    // - RetroArch not installed (no /retroarch): NotFound, nothing created.
    // - retroarch.cfg missing: created with just this key (RetroArch fills
    //   in the defaults on its next launch).
    // - Before the first modification of an existing file, a pristine copy
    //   is kept as retroarch.cfg.rmbak.
    // - Nothing is written when the value is already right.
    Status setBrowserDirectory(const std::string& directory);

    std::string backupPath() const { return layout_.retroarchCfg + ".rmbak"; }

  private:
    IFileSystem& fs_;
    SdLayout layout_;
};

}  // namespace rm
