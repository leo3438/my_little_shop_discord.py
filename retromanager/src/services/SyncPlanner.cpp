#include "retromanager/services/SyncPlanner.hpp"

#include <cstdlib>
#include <nlohmann/json.hpp>
#include <set>

namespace rm {

namespace {

bool looksIdentical(const FileState& a, const FileState& b, std::int64_t tolerance) {
    return a.size == b.size && std::llabs(a.modifiedAt - b.modifiedAt) <= tolerance;
}

SyncAction newestWins(const FileState& local, const FileState& remote) {
    return local.modifiedAt >= remote.modifiedAt ? SyncAction::ConflictKeepLocal : SyncAction::ConflictKeepRemote;
}

}  // namespace

std::vector<PlannedAction> planSync(const std::map<std::string, FileState>& local,
                                    const std::map<std::string, FileState>& remote,
                                    const std::map<std::string, SyncRecord>& previous, std::int64_t tolerance) {
    std::set<std::string> paths;
    for (const auto& entry : local) paths.insert(entry.first);
    for (const auto& entry : remote) paths.insert(entry.first);

    std::vector<PlannedAction> plan;
    for (const std::string& path : paths) {
        auto l = local.find(path);
        auto r = remote.find(path);
        if (r == remote.end()) {
            plan.push_back({path, SyncAction::Upload});
            continue;
        }
        if (l == local.end()) {
            plan.push_back({path, SyncAction::Download});
            continue;
        }

        const FileState& now = l->second;
        const FileState& there = r->second;
        auto record = previous.find(path);
        if (record == previous.end()) {
            // Never synced: identical files become the baseline, anything
            // else is a first-contact conflict.
            plan.push_back({path, looksIdentical(now, there, tolerance) ? SyncAction::InSync : newestWins(now, there)});
            continue;
        }

        bool localChanged = now != record->second.local;
        bool remoteChanged = there != record->second.remote;
        SyncAction action = SyncAction::InSync;
        if (localChanged && !remoteChanged) action = SyncAction::Upload;
        else if (!localChanged && remoteChanged) action = SyncAction::Download;
        else if (localChanged && remoteChanged) action = looksIdentical(now, there, tolerance) ? SyncAction::InSync : newestWins(now, there);
        plan.push_back({path, action});
    }
    return plan;
}

// --- sync-state.json ---------------------------------------------------------

namespace {

using json = nlohmann::json;

Result<FileState> readState(const json& object, const char* side) {
    auto it = object.find(side);
    if (it == object.end() || !it->is_object()) return makeError(ErrorCode::ParseError, std::string("missing \"") + side + "\"");
    auto size = it->find("size");
    auto mtime = it->find("mtime");
    if (size == it->end() || !size->is_number_unsigned() || mtime == it->end() || !mtime->is_number_integer()) {
        return makeError(ErrorCode::ParseError, std::string("invalid \"") + side + "\" state");
    }
    return FileState{size->get<std::uint64_t>(), mtime->get<std::int64_t>()};
}

}  // namespace

Result<SyncState> parseSyncState(std::string_view document) {
    json root;
    try {
        root = json::parse(document.begin(), document.end());
    } catch (const json::parse_error& e) {
        return makeError(ErrorCode::ParseError, e.what());
    }
    if (!root.is_object()) return makeError(ErrorCode::ParseError, "sync state root must be an object");
    if (auto v = root.find("version"); v != root.end() && v->is_number_integer() && v->get<int>() > SyncState::kVersion) {
        return makeError(ErrorCode::Unsupported, "sync state written by a newer RetroManager");
    }

    SyncState state;
    if (auto url = root.find("remote"); url != root.end() && url->is_string()) state.remoteUrl = url->get<std::string>();
    auto files = root.find("files");
    if (files == root.end() || !files->is_object()) return makeError(ErrorCode::ParseError, "\"files\" must be an object");
    for (auto it = files->begin(); it != files->end(); ++it) {
        if (!it->is_object()) return makeError(ErrorCode::ParseError, "invalid record for " + it.key());
        auto l = readState(*it, "local");
        if (!l) return l.error();
        auto r = readState(*it, "remote");
        if (!r) return r.error();
        state.files[it.key()] = SyncRecord{l.value(), r.value()};
    }
    return state;
}

std::string serializeSyncState(const SyncState& state) {
    nlohmann::ordered_json root;
    root["version"] = SyncState::kVersion;
    root["remote"] = state.remoteUrl;
    root["files"] = nlohmann::ordered_json::object();
    for (const auto& [path, record] : state.files) {
        root["files"][path]["local"] = {{"size", record.local.size}, {"mtime", record.local.modifiedAt}};
        root["files"][path]["remote"] = {{"size", record.remote.size}, {"mtime", record.remote.modifiedAt}};
    }
    return root.dump(2) + "\n";
}

}  // namespace rm
