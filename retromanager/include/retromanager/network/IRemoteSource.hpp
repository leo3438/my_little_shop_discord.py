#pragma once

#include <string>

#include "retromanager/core/Result.hpp"

namespace rm {

// A place a shop lives: an FTP share on a NAS, an HTTP server, an SMB share,
// or an in-memory mock.
//
// The network layer only moves bytes. It knows nothing about the index
// format: parsing is the job of RepoIndexParser, orchestrated by ShopService.
//
// Implementations are called from a background thread (see ITaskRunner) and
// must not touch the UI. Each call is independent: an implementation may
// open a connection per call.
class IRemoteSource {
  public:
    virtual ~IRemoteSource() = default;

    // Human readable location, safe to display and log (never contains a
    // password), e.g. "ftp://leo@nas.local:21/shop/index.json".
    virtual std::string describe() const = 0;

    // Absolute URL of the index, used to resolve relative entries. Empty
    // when the source has no meaningful URL.
    virtual std::string indexUrl() const = 0;

    // Downloads the raw shop index document.
    virtual Result<std::string> fetchIndex() = 0;

    // Phase 3: openRead(url) -> IReadStream, to stream ROMs to the SD card.
};

}  // namespace rm
