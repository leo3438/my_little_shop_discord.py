#pragma once

#include <functional>
#include <string>
#include <vector>

#include "retromanager/core/ITaskRunner.hpp"
#include "retromanager/core/Result.hpp"
#include "retromanager/fs/RomStore.hpp"
#include "retromanager/models/GameEntry.hpp"
#include "retromanager/network/IRemoteSource.hpp"

namespace rm {

// "La Boutique": fetches a shop index from a remote source and turns it into
// models the UI can display. This is the only entry point the UI uses: it
// never sees the parser nor the transport.
class ShopService {
  public:
    using ListingCallback = std::function<void(Result<ShopListing>)>;

    // Dependencies must outlive the service. Without a store, nothing is
    // reported as installed.
    ShopService(IRemoteSource& source, ITaskRunner& tasks, RomStore* store = nullptr);

    std::string sourceDescription() const { return source_.describe(); }

    // Blocking: fetch + parse on the calling thread.
    Result<RepoIndex> loadIndex();

    // Blocking: loadIndex() plus the SD card check of every entry.
    Result<ShopListing> loadListing();

    // loadListing() in the background; `onDone` runs on the main thread.
    void loadIndexAsync(ListingCallback onDone);

    // Sections sorted by display name, games sorted by title within each
    // section (case-insensitive). Unknown systems come last.
    static std::vector<SystemSection> groupBySystem(const std::vector<GameEntry>& games);

  private:
    IRemoteSource& source_;
    ITaskRunner& tasks_;
    RomStore* store_;
};

}  // namespace rm
