#include "CurlCommon.hpp"

#include <mutex>

namespace rm::curl {

void ensureInitialized() {
    // Never paired with curl_global_cleanup: the process exit reclaims everything.
    static std::once_flag once;
    std::call_once(once, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });
}

Error fromCode(CURLcode code, const char* details, const std::string& where) {
    // "ftp /shop/index.json: cURL error 67 (Login denied): Access denied: 530"
    // The code and curl's name for it are what forums and docs search for;
    // the error buffer adds the server's own words.
    std::string message = where + ": cURL error " + std::to_string(static_cast<int>(code)) + " (" +
                          curl_easy_strerror(code) + ")";
    if (details != nullptr && details[0] != '\0') {
        std::string extra = details;
        while (!extra.empty() && (extra.back() == '\n' || extra.back() == '\r')) extra.pop_back();
        if (extra != curl_easy_strerror(code)) message += ": " + extra;
    }
    switch (code) {
        case CURLE_LOGIN_DENIED: return makeError(ErrorCode::AuthenticationFailed, message);
        case CURLE_REMOTE_ACCESS_DENIED: return makeError(ErrorCode::PermissionDenied, message);
        case CURLE_REMOTE_FILE_NOT_FOUND: return makeError(ErrorCode::NotFound, message);
        case CURLE_ABORTED_BY_CALLBACK: return makeError(ErrorCode::Cancelled, where + ": cancelled");
        case CURLE_UNSUPPORTED_PROTOCOL:
        case CURLE_NOT_BUILT_IN: return makeError(ErrorCode::Unsupported, message);
        default: return makeError(ErrorCode::NetworkError, message);
    }
}

}  // namespace rm::curl
