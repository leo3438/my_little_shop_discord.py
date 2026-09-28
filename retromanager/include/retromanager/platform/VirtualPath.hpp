#pragma once

#include <string>
#include <string_view>

#include "retromanager/core/Result.hpp"

// Virtual paths are the only path format the application manipulates.
//
// A virtual path is POSIX-style, absolute and rooted at the SD card:
// "/retroarch/retroarch.cfg" means "sdmc:/retroarch/retroarch.cfg" on the
// console and "<mock root>/retroarch/retroarch.cfg" on desktop. Mapping a
// virtual path to a real location is the job of an IFileSystem implementation
// and nothing else.
namespace rm::vpath {

// Collapses separators, resolves "." and "..", converts '\' to '/', strips
// the trailing '/'. Fails with InvalidPath for relative paths, empty paths,
// NUL bytes, or ".." escaping the root.
Result<std::string> normalize(std::string_view path);

// Appends a relative child to a base path, then normalizes the result.
Result<std::string> join(std::string_view base, std::string_view child);

// The following helpers expect an already normalized path.

// "/a/b" -> "/a", "/a" -> "/", "/" -> "/".
std::string parent(std::string_view path);

// "/a/game.sfc" -> "game.sfc", "/" -> "".
std::string filename(std::string_view path);

// "/a/game.sfc" -> "game", "/a/.hidden" -> ".hidden".
std::string stem(std::string_view path);

// "/a/game.sfc" -> ".sfc", "/a/archive" -> "", "/a/.hidden" -> "".
std::string extension(std::string_view path);

// True when `path` is `ancestor` or lies beneath it.
bool isWithin(std::string_view path, std::string_view ancestor);

}  // namespace rm::vpath
