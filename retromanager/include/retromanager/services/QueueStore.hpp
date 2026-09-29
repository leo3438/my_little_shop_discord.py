#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "retromanager/core/Result.hpp"
#include "retromanager/platform/IFileSystem.hpp"

namespace rm {

class AppManager;
class DownloadQueueManager;

// /switch/RetroManager/queue.json: the downloads still to do (running one
// first), as queue payloads (EntryJson.hpp). Written atomically on every
// change of the queue; read once at startup.
class QueueStore {
  public:
    QueueStore(IFileSystem& fs, std::string path);

    Status save(const std::vector<std::string>& payloads);
    // Missing or damaged file: empty (a broken file never blocks the app).
    std::vector<std::string> load() const;

    const std::string& path() const { return path_; }

  private:
    IFileSystem& fs_;
    std::string path_;
};

// Queues the saved items again: ROMs directly, homebrews through
// AppManager::job(). Unreadable payloads are skipped. Returns how many were
// queued. Interrupted downloads resume from their .tmp file.
std::size_t restoreQueue(const std::vector<std::string>& payloads, DownloadQueueManager& queue, AppManager& apps);

}  // namespace rm
