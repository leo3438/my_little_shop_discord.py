#pragma once

#include <optional>
#include <string>

#include "retromanager/core/Cancellation.hpp"
#include "retromanager/core/Result.hpp"
#include "retromanager/models/GameEntry.hpp"

namespace rm {

// Work done once a ROM is safely on the SD card: configure the emulator,
// fetch cheats, and later scrape artwork, create a forwarder...
//
// DownloadService runs the registered steps in order after each successful
// install. A failing step never undoes the install: the ROM is there, the
// step's error is reported next to it.
class IPostInstallStep {
  public:
    virtual ~IPostInstallStep() = default;

    // Stable identifier, used by the UI to label the outcome ("retroarch", "cheats").
    virtual std::string id() const = 0;

    // `romPath` is the installed ROM's virtual path. nullopt when the step
    // does not apply to this game (it then does not appear in the report).
    virtual std::optional<Status> run(const GameEntry& game, const std::string& romPath,
                                      const CancellationToken& cancel) = 0;
};

struct StepOutcome {
    std::string id;
    Status result;
};

}  // namespace rm
