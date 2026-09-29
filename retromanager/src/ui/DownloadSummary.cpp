#include "retromanager/ui/DownloadSummary.hpp"

#include <algorithm>
#include <borealis.hpp>
#include <cmath>

#include "retromanager/core/Format.hpp"
#include "retromanager/models/Systems.hpp"

namespace rm::ui {

namespace {

std::string describeFailure(const DownloadOutcome& outcome) {
    const Error& error = outcome.result.error();
    switch (error.code) {
        case ErrorCode::Cancelled: return brls::getStr("retromanager/downloads/cancelled");
        case ErrorCode::InsufficientSpace:
            return brls::getStr("retromanager/download/error_space", formatBytes(outcome.space.requiredBytes),
                                formatBytes(outcome.space.availableBytes.value_or(0)));
        case ErrorCode::NetworkError: return brls::getStr("retromanager/downloads/error_network_resumable");
        case ErrorCode::AuthenticationFailed: return brls::getStr("retromanager/shop/error_auth");
        case ErrorCode::NotFound: return brls::getStr("retromanager/download/error_not_found");
        case ErrorCode::IntegrityError: return brls::getStr("retromanager/download/error_integrity");
        case ErrorCode::PermissionDenied: return brls::getStr("retromanager/download/error_denied");
        case ErrorCode::NotConfigured: return brls::getStr("retromanager/shop/error_not_configured");
        default: return brls::getStr("retromanager/download/error_generic");
    }
}

std::string parentFolder(const std::string& path) {
    std::size_t slash = path.rfind('/');
    return slash == std::string::npos ? path : path.substr(0, slash + 1);
}

std::string describeStep(const StepOutcome& step, const DownloadOutcome& outcome) {
    const std::string folder = parentFolder(outcome.destination);
    const bool ok = step.result.ok();
    const bool missing = !ok && step.result.error().code == ErrorCode::NotFound;
    auto pick = [&](const char* okKey, const char* missingKey, const char* failedKey, std::string okArg = "") {
        if (ok) return okArg.empty() ? brls::getStr(okKey) : brls::getStr(okKey, okArg);
        if (missing && missingKey != nullptr) return brls::getStr(missingKey);
        return brls::getStr(failedKey, step.result.error().describe());
    };
    if (step.id == "retroarch") {
        return pick("retromanager/download/step_retroarch_ok", "retromanager/download/step_retroarch_missing",
                    "retromanager/download/step_retroarch_failed", folder);
    }
    if (step.id == "playlist") {
        const SystemInfo* system = systems::find(outcome.system);
        return pick("retromanager/download/step_playlist_ok", "retromanager/download/step_playlist_missing",
                    "retromanager/download/step_playlist_failed", system ? system->libretroName : outcome.system);
    }
    if (step.id == "boxart") {
        return pick("retromanager/download/step_boxart_ok", "retromanager/download/step_boxart_missing",
                    "retromanager/download/step_boxart_failed");
    }
    if (step.id == "icon") {
        return pick("retromanager/download/step_icon_ok", "retromanager/download/step_icon_missing",
                    "retromanager/download/step_icon_failed", folder);
    }
    if (step.id == "record") return pick("", nullptr, "retromanager/download/step_record_failed");
    if (step.id == "cheats") {
        return pick("retromanager/download/step_cheats_ok", "retromanager/download/step_cheats_missing",
                    "retromanager/download/step_cheats_failed");
    }
    if (step.id == "sysclk") {
        return pick("retromanager/download/step_sysclk_ok", "retromanager/download/step_sysclk_missing",
                    "retromanager/download/step_sysclk_failed");
    }
    return step.id + ": " + (ok ? "OK" : step.result.error().describe());
}

}  // namespace

std::string outcomeHeadline(const DownloadOutcome& outcome) {
    if (!outcome.result.ok()) return describeFailure(outcome);
    if (outcome.kind == DownloadKind::App) {
        return brls::getStr("retromanager/download/app_done", parentFolder(outcome.destination));
    }
    return brls::getStr("retromanager/download/done");
}

std::string outcomeDetails(const DownloadOutcome& outcome) {
    std::string report;
    for (const StepOutcome& step : outcome.steps) {
        if (!report.empty()) report += "\n";
        report += describeStep(step, outcome);
    }
    return report;
}

std::string progressLine(const QueueItem& item) {
    std::string speed = formatBytes(static_cast<std::uint64_t>(std::llround(item.bytesPerSecond))) + "/s";
    std::string line;
    if (item.total > 0) {
        double ratio = std::min(1.0, static_cast<double>(item.received) / static_cast<double>(item.total));
        line = std::to_string(static_cast<int>(ratio * 100.0)) + " %   ·   " + formatBytes(item.received) + " / " +
               formatBytes(item.total) + "   ·   " + speed;
    } else {
        line = formatBytes(item.received) + "   ·   " + speed;
    }
    if (item.resumedFrom > 0) line += "   ·   " + brls::getStr("retromanager/downloads/resumed", formatBytes(item.resumedFrom));
    return line;
}

}  // namespace rm::ui
