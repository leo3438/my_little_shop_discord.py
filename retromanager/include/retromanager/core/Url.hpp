#pragma once

#include <string>
#include <string_view>

#include "retromanager/core/Result.hpp"

// Minimal URL helpers for shop indexes. Not a general-purpose URL library:
// just what's needed to resolve entries and name files.
namespace rm::url {

// "ftp:", "https:", "smb:"... per RFC 3986 (ALPHA *(ALPHA / DIGIT / "+" / "-" / ".") ":").
bool hasScheme(std::string_view url);

// "%20" -> " ". Malformed escapes are kept verbatim.
std::string percentDecode(std::string_view value);

// Escapes everything but unreserved characters and '/'.
std::string percentEncodePath(std::string_view path);

// Resolves `reference` against `base` (RFC 3986 subset: absolute, "//host",
// "/absolute-path", "relative/path", with "." and ".." removal).
// Fails with InvalidArgument for a relative reference without a usable base.
Result<std::string> resolve(std::string_view base, std::string_view reference);

// Part after '#', undecoded. Empty when absent.
std::string fragment(std::string_view url);

// URL without its fragment.
std::string stripFragment(std::string_view url);

// Last path segment, query and fragment removed, percent-decoded.
// "ftp://nas/roms/Super%20Mario.sfc?x=1" -> "Super Mario.sfc".
std::string lastPathSegment(std::string_view url);

}  // namespace rm::url
