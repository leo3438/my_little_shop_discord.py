#pragma once

#include <set>
#include <string>

#include "retromanager/platform/IFileSystem.hpp"
#include "retromanager/platform/SdLayout.hpp"
#include "retromanager/services/PostInstallStep.hpp"

namespace rm {

// After installing a game for a demanding system (N64, PlayStation, 3DS),
// makes sys-clk run the CPU at full speed while RetroArch is in front:
// [<RetroArch title id>] handheld_cpu=1785 / docked_cpu=1785 in
// /config/sys-clk/config.ini, edited with IniDocument (the rest of the file
// is left untouched).
//
// sys-clk applies profiles per *title id of the running program*: the id to
// use depends on how RetroArch is launched (installed forwarder, or the
// .nro from the Album / a hijacked game). It is therefore configurable in
// config.json ("sysclk.title_id").
class SysClkConfigurator : public IPostInstallStep {
  public:
    static constexpr const char* kDefaultTitleId = "05B9D58000000000";
    static constexpr const char* kMaxCpuMhz = "1785";  // highest stock CPU clock

    SysClkConfigurator(IFileSystem& fs, SdLayout layout, std::string titleId = kDefaultTitleId,
                       std::set<std::string> systems = {"n64", "psx", "3ds"});

    std::string id() const override { return "sysclk"; }

    // nullopt for systems that run fine at stock clocks.
    std::optional<Status> run(const GameEntry& game, const std::string& romPath,
                              const CancellationToken& cancel) override;

    // - sys-clk not installed (no /config/sys-clk): NotFound, nothing created.
    // - config.ini missing: created with just this profile.
    // - Existing file: pristine copy kept as config.ini.rmbak before the
    //   first modification; nothing written if already at full speed.
    Status applyMaxCpuProfile();

    // 16 hexadecimal digits.
    static bool isValidTitleId(const std::string& titleId);

    std::string backupPath() const { return layout_.sysClkConfig + ".rmbak"; }

  private:
    IFileSystem& fs_;
    SdLayout layout_;
    std::string titleId_;
    std::set<std::string> systems_;
};

}  // namespace rm
