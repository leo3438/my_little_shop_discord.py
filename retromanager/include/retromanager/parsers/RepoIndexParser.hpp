#pragma once

#include <string>
#include <string_view>

#include "retromanager/core/Result.hpp"
#include "retromanager/models/GameEntry.hpp"

namespace rm {

// Parses a shop index (JSON) into a RepoIndex. Pure function of its input:
// no I/O, no global state. Format reference: docs/INDEX_FORMAT.md.
//
// Document-level problems (malformed JSON, wrong root type, unsupported
// version, no "games"/"files" array) fail the whole parse. Entry-level
// problems only skip the faulty entry and add a warning, so one bad line in
// a hand-written index does not hide the rest of the shop.
class RepoIndexParser {
  public:
    static constexpr int kSupportedVersion = 1;

    // `baseUrl` is where the index was fetched from; relative entry URLs are
    // resolved against it. Leave empty when unknown (relative entries are
    // then skipped).
    explicit RepoIndexParser(std::string baseUrl = "");

    Result<RepoIndex> parse(std::string_view document) const;

  private:
    std::string baseUrl_;
};

}  // namespace rm
