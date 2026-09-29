#pragma once

#include <string>
#include <string_view>

#include "retromanager/core/Result.hpp"
#include "retromanager/models/AppEntry.hpp"
#include "retromanager/models/GameEntry.hpp"

namespace rm {

// GameEntry / AppEntry <-> JSON object text, every field kept: what the
// download queue writes to queue.json to rebuild its jobs at the next
// launch. Pure functions, no I/O.
std::string gameToJson(const GameEntry& game);
Result<GameEntry> gameFromJson(std::string_view json);  // ParseError without url / file name
std::string appToJson(const AppEntry& app);
Result<AppEntry> appFromJson(std::string_view json);  // ParseError without id / nro_url / folder

// A queue item: {"kind": "rom" | "app", "entry": {...}}.
std::string queuePayload(const GameEntry& game);
std::string queuePayload(const AppEntry& app);

}  // namespace rm
