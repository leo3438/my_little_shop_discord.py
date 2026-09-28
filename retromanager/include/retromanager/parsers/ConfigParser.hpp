#pragma once

#include <string>
#include <string_view>

#include "retromanager/core/Result.hpp"
#include "retromanager/models/AppConfig.hpp"

namespace rm {

// config.json <-> AppConfig. Pure functions, no I/O.
//
// Missing fields take their default value, unknown fields are ignored
// (forward compatibility), a field with the wrong type is an error: silently
// replacing a typo'd password with a default would be worse than refusing.
Result<AppConfig> parseConfig(std::string_view document);

// Pretty-printed, stable key order, trailing newline.
std::string serializeConfig(const AppConfig& config);

}  // namespace rm
