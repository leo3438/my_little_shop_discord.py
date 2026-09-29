#pragma once

// Internal to the network layer: shared libcurl plumbing (not a public header).

#include <curl/curl.h>

#include <string>

#include "retromanager/core/Result.hpp"

namespace rm::curl {

// curl_global_init is not thread-safe: every client calls this before its
// first handle, and it runs exactly once per process.
void ensureInitialized();

// Transport-level curl failure -> ErrorCode ("where" prefixes the message).
Error fromCode(CURLcode code, const char* details, const std::string& where);

}  // namespace rm::curl
