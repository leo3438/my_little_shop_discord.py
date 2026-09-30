#include <switch.h>

#include <cstdio>
#include <string>

#include "retromanager/platform/LocalFileSystem.hpp"
#include "retromanager/platform/Platform.hpp"

namespace rm {

namespace {

class SwitchSystem : public ISystem {
  public:
    // "Media playback" mode: the console neither dims the screen nor enters
    // sleep mode on its own. Best effort: a failure (unusual applet mode)
    // only means the console may sleep, never a failed download.
    void setKeepAwake(bool keepAwake) override { (void)appletSetMediaPlaybackState(keepAwake); }
};

}  // namespace

// libnx mounts the SD card as "sdmc:" before main(); newlib's stdio and
// std::filesystem both understand that prefix.
PlatformServices createPlatformServices() {
    auto sd = std::make_shared<LocalFileSystem>("sdmc:/");
    sd->setCommitHook([]() -> Status {
        ::Result rc = fsdevCommitDevice("sdmc");  // libnx result code, not rm::Result
        if (R_FAILED(rc)) {
            char code[32];
            std::snprintf(code, sizeof code, "0x%X (%04u-%04u)", static_cast<unsigned>(rc), 2000u + R_MODULE(rc), R_DESCRIPTION(rc));
            return makeError(ErrorCode::IoError, std::string("fsdevCommitDevice(sdmc) failed: ") + code);
        }
        return success();
    });
    return PlatformServices{
        "Nintendo Switch",
        "sdmc:/",
        std::move(sd),
        std::make_shared<SwitchSystem>(),
    };
}

}  // namespace rm
