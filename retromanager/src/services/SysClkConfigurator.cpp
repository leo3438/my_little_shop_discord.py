#include "retromanager/services/SysClkConfigurator.hpp"

#include <algorithm>
#include <cctype>

#include "retromanager/parsers/IniDocument.hpp"
#include "retromanager/platform/VirtualPath.hpp"

namespace rm {

SysClkConfigurator::SysClkConfigurator(IFileSystem& fs, SdLayout layout, std::string titleId,
                                       std::set<std::string> systems)
    : fs_(fs), layout_(std::move(layout)), titleId_(std::move(titleId)), systems_(std::move(systems)) {}

bool SysClkConfigurator::isValidTitleId(const std::string& titleId) {
    return titleId.size() == 16 &&
           std::all_of(titleId.begin(), titleId.end(), [](unsigned char c) { return std::isxdigit(c) != 0; });
}

std::optional<Status> SysClkConfigurator::run(const GameEntry& game, const std::string&, const CancellationToken&) {
    if (systems_.count(game.system) == 0) return std::nullopt;
    return applyMaxCpuProfile();
}

Status SysClkConfigurator::applyMaxCpuProfile() {
    if (!isValidTitleId(titleId_)) {
        return makeError(ErrorCode::InvalidArgument, "invalid sys-clk title id \"" + titleId_ + "\" (16 hex digits)");
    }
    const std::string directory = vpath::parent(layout_.sysClkConfig);
    if (!fs_.isDirectory(directory)) {
        return makeError(ErrorCode::NotFound, "sys-clk is not installed (" + directory + " is missing)");
    }

    auto existing = fs_.readFile(layout_.sysClkConfig);
    if (!existing && existing.error().code != ErrorCode::NotFound) return existing.error();
    const bool fileExists = existing.ok();

    IniDocument ini = IniDocument::parse(fileExists ? existing.value() : std::string());
    for (const char* key : {"handheld_cpu", "docked_cpu"}) {
        if (Status set = ini.set(titleId_, key, kMaxCpuMhz); !set) return set;
    }
    if (!ini.modified()) return success();

    if (fileExists && !fs_.exists(backupPath())) {
        if (Status backup = fs_.writeFile(backupPath(), existing.value()); !backup) return backup;
    }
    return fs_.writeFile(layout_.sysClkConfig, ini.serialize());  // atomic replace
}

}  // namespace rm
