#pragma once

#include <string>
#include <string_view>

#include "retromanager/core/Result.hpp"
#include "retromanager/models/AppConfig.hpp"

namespace rm {

// config.json <-> AppConfig. Pure functions, no I/O.
//
// config.json is edited by hand, so the parser is forgiving:
// - syntax: UTF-8 BOM, // and /* */ comments, trailing commas;
// - sources: key aliases (user/login, pass, index_url...), types in any case
//   ("FTP", "https", "web"), a bare URL string, a single object instead of
//   an array, numbers for user names and passwords;
// - a source entry that still makes no sense is skipped, never defaulted
//   (a typo'd password must not silently become another one), and every
//   other source is kept; a wrong top-level field keeps its default. Both
//   leave a line in AppConfig::warnings.
// Only a document that is not JSON (with the line and column) or written by
// a newer version is an error. Missing fields take their default value,
// unknown fields are ignored.
Result<AppConfig> parseConfig(std::string_view document);

// Pretty-printed, stable key order, trailing newline.
std::string serializeConfig(const AppConfig& config);

}  // namespace rm
