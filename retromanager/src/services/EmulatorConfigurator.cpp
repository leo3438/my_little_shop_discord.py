#include "retromanager/services/EmulatorConfigurator.hpp"

#include "retromanager/parsers/CfgDocument.hpp"
#include "retromanager/platform/VirtualPath.hpp"

namespace rm {

EmulatorConfigurator::EmulatorConfigurator(IFileSystem& fs, SdLayout layout) : fs_(fs), layout_(std::move(layout)) {}

std::optional<Status> EmulatorConfigurator::run(const GameEntry&, const std::string& romPath, const CancellationToken&) {
    std::string directory = vpath::parent(romPath);
    if (directory.back() != '/') directory += '/';  // RetroArch-style directory value: "/roms/nds/"
    return setBrowserDirectory(directory);
}

Status EmulatorConfigurator::setBrowserDirectory(const std::string& directory) {
    if (!fs_.isDirectory(layout_.retroarchDir)) {
        return makeError(ErrorCode::NotFound, "RetroArch is not installed (" + layout_.retroarchDir + " is missing)");
    }

    auto existing = fs_.readFile(layout_.retroarchCfg);
    if (!existing && existing.error().code != ErrorCode::NotFound) return existing.error();
    const bool fileExists = existing.ok();

    CfgDocument cfg = CfgDocument::parse(fileExists ? existing.value() : std::string());
    if (Status set = cfg.set(kBrowserDirectoryKey, directory); !set) return set;
    if (!cfg.modified()) return success();  // already pointing there: no write at all

    // First edit of a user's file: keep the original around.
    if (fileExists && !fs_.exists(backupPath())) {
        if (Status backup = fs_.writeFile(backupPath(), existing.value()); !backup) return backup;
    }
    return fs_.writeFile(layout_.retroarchCfg, cfg.serialize());  // atomic replace
}

}  // namespace rm
