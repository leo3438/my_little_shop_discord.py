#include "MockSdCard.hpp"

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <system_error>

#include "retromanager/platform/VirtualPath.hpp"

#ifndef RM_FIXTURE_SD_DIR
#error "RM_FIXTURE_SD_DIR must be defined by the build system"
#endif

namespace rm::test {

std::filesystem::path fixtureSdCardDir() { return RM_FIXTURE_SD_DIR; }

Status copyHostTree(const std::filesystem::path& hostDir, IFileSystem& target, const std::string& virtualDir) {
    if (Status created = target.createDirectories(virtualDir); !created) return created;

    std::error_code ec;
    for (std::filesystem::directory_iterator it(hostDir, ec), end; !ec && it != end; it.increment(ec)) {
        std::string name = it->path().filename().string();
        auto child = vpath::join(virtualDir, name);
        if (!child) return child.error();

        if (it->is_directory()) {
            if (Status copied = copyHostTree(it->path(), target, child.value()); !copied) return copied;
        } else if (it->is_regular_file() && name != ".gitkeep") {
            std::ifstream in(it->path(), std::ios::binary);
            std::ostringstream content;
            content << in.rdbuf();
            if (Status written = target.writeFile(child.value(), content.str()); !written) return written;
        }
    }
    if (ec) return makeError(ErrorCode::IoError, hostDir.string() + ": " + ec.message());
    return success();
}

std::unique_ptr<MemoryFileSystem> makeMockSdCard() {
    auto fs = std::make_unique<MemoryFileSystem>();
    Status loaded = copyHostTree(fixtureSdCardDir(), *fs);
    if (!loaded) throw std::runtime_error("cannot load fixture SD card: " + loaded.error().describe());
    return fs;
}

}  // namespace rm::test
