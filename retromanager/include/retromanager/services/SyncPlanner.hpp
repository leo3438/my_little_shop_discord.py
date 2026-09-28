#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "retromanager/core/Result.hpp"

namespace rm {

// Size and modification time of one side of a synchronized file.
struct FileState {
    std::uint64_t size = 0;
    std::int64_t modifiedAt = 0;  // Unix seconds; 0 when the side cannot tell

    bool operator==(const FileState& o) const { return size == o.size && modifiedAt == o.modifiedAt; }
    bool operator!=(const FileState& o) const { return !(*this == o); }
};

// Both sides as they were right after the last successful sync of a file.
struct SyncRecord {
    FileState local;
    FileState remote;
};

// Persistent memory of the last sync (sync-state.json).
struct SyncState {
    static constexpr int kVersion = 1;
    std::string remoteUrl;                    // records are only valid for this remote
    std::map<std::string, SyncRecord> files;  // key: path relative to the saves folder, '/'-separated
};

Result<SyncState> parseSyncState(std::string_view document);
std::string serializeSyncState(const SyncState& state);

enum class SyncAction {
    InSync,              // nothing to transfer
    Upload,              // local -> NAS
    Download,            // NAS -> local
    ConflictKeepLocal,   // both changed, local is newer: keep a copy of the NAS version, then upload
    ConflictKeepRemote,  // both changed, NAS is newer: keep a copy of the local version, then download
};

struct PlannedAction {
    std::string path;
    SyncAction action;

    bool operator==(const PlannedAction& o) const { return path == o.path && action == o.action; }
};

// Decides what to do with every file, from what each side looks like now
// and what they looked like after the last sync.
//
// Comparing a side with its own previous state (not local mtime against
// NAS mtime) is what makes the result independent of clock skew, FAT's 2 s
// timestamps and the fact that an upload gets the server's clock: a file
// synced once is never bounced back and forth.
//
// The "newest wins" rule only decides conflicts (both sides changed, or two
// different versions met for the first time), and the losing version is
// always kept as a conflict copy: a sync never destroys a save.
//
// Deletions are not propagated: a file missing on one side is copied back.
// `tolerance` is the timestamp slack used to recognise identical files on a
// first sync (FAT stores 2 s steps).
std::vector<PlannedAction> planSync(const std::map<std::string, FileState>& local,
                                    const std::map<std::string, FileState>& remote,
                                    const std::map<std::string, SyncRecord>& previous, std::int64_t tolerance = 2);

}  // namespace rm
