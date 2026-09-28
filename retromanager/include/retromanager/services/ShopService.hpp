#pragma once

#include <functional>
#include <string>
#include <vector>

#include "retromanager/core/ITaskRunner.hpp"
#include "retromanager/core/Result.hpp"
#include "retromanager/models/GameEntry.hpp"
#include "retromanager/network/IRemoteSource.hpp"

namespace rm {

// "La Boutique": fetches a shop index from a remote source and turns it into
// models the UI can display. This is the only entry point the UI uses: it
// never sees the parser nor the transport.
class ShopService {
  public:
    using IndexCallback = std::function<void(Result<RepoIndex>)>;

    // Both dependencies must outlive the service.
    ShopService(IRemoteSource& source, ITaskRunner& tasks);

    std::string sourceDescription() const { return source_.describe(); }

    // Blocking: fetch + parse on the calling thread.
    Result<RepoIndex> loadIndex();

    // Fetch + parse in the background; `onDone` runs on the main thread.
    void loadIndexAsync(IndexCallback onDone);

    // Sections sorted by display name, games sorted by title within each
    // section (case-insensitive). Unknown systems come last.
    static std::vector<SystemSection> groupBySystem(const std::vector<GameEntry>& games);

  private:
    IRemoteSource& source_;
    ITaskRunner& tasks_;
};

}  // namespace rm
