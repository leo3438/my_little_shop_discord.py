#include <switch.h>

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
    return PlatformServices{
        "Nintendo Switch",
        "sdmc:/",
        std::make_shared<LocalFileSystem>("sdmc:/"),
        std::make_shared<SwitchSystem>(),
    };
}

}  // namespace rm
