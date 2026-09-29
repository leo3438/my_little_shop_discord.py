#include "retromanager/network/HttpClient.hpp"

#include <curl/curl.h>

#include <memory>

#include "CurlCommon.hpp"
#include "retromanager/core/Url.hpp"

namespace rm {

namespace {

constexpr long kReceiveBufferBytes = 256 * 1024;

struct TransferState {
    CURL* handle;
    const ChunkSink* sink;
    const ProgressCallback* progress;
    const CancellationToken* cancel;
    std::uint64_t offset;
    bool statusChecked = false;
    std::optional<Error> error;  // status or sink failure
};

std::size_t onData(char* chunk, std::size_t size, std::size_t count, void* userData) {
    auto* state = static_cast<TransferState*>(userData);
    if (state->cancel->isCancelled()) return 0;
    if (!state->statusChecked) {
        // First body bytes: make sure they are the file (not an error page,
        // not the whole file when a range was asked).
        state->statusChecked = true;
        long status = 0;
        curl_easy_getinfo(state->handle, CURLINFO_RESPONSE_CODE, &status);
        if (auto error = HttpClient::errorForStatus(status, state->offset)) {
            state->error = error;
            return 0;
        }
    }
    std::size_t bytes = size * count;
    Status accepted = (*state->sink)(chunk, bytes);
    if (!accepted) {
        state->error = accepted.error();
        return 0;
    }
    return bytes;
}

int onProgress(void* userData, curl_off_t dlTotal, curl_off_t dlNow, curl_off_t, curl_off_t) {
    auto* state = static_cast<TransferState*>(userData);
    if (state->cancel->isCancelled()) return 1;  // -> CURLE_ABORTED_BY_CALLBACK
    if (*state->progress && dlNow > 0 && state->statusChecked && !state->error) {
        std::uint64_t total = dlTotal > 0 ? state->offset + static_cast<std::uint64_t>(dlTotal) : 0;
        (*state->progress)(TransferProgress{state->offset + static_cast<std::uint64_t>(dlNow), total});
    }
    return 0;
}

std::string lower(std::string value) {
    for (char& c : value) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return value;
}

std::uint16_t defaultPort(const std::string& scheme) { return scheme == "https" ? 443 : 80; }

}  // namespace

HttpClient::HttpClient(HttpConfig config) : config_(std::move(config)) {}

Status HttpClient::validate(const HttpConfig& config) {
    auto parts = url::split(config.indexUrl);
    if (!parts) return makeError(ErrorCode::InvalidArgument, "invalid index URL: " + config.indexUrl);
    if (parts.value().scheme != "http" && parts.value().scheme != "https") {
        return makeError(ErrorCode::InvalidArgument, "HTTP source needs an http:// or https:// URL: " + config.indexUrl);
    }
    if (config.maxIndexBytes == 0) return makeError(ErrorCode::InvalidArgument, "maxIndexBytes must be positive");
    return success();
}

std::optional<Error> HttpClient::errorForStatus(long status, std::uint64_t offset) {
    const std::string code = "HTTP " + std::to_string(status);
    if (offset > 0) {
        if (status == 206) return std::nullopt;
        if (status == 200) return makeError(ErrorCode::Unsupported, code + ": the server ignored the Range request");
        if (status == 416) return makeError(ErrorCode::Unsupported, code + ": range not satisfiable");
    } else {
        if (status == 200) return std::nullopt;
        if (status == 206) return makeError(ErrorCode::NetworkError, code + ": partial answer to a full request");
    }
    if (status == 401 || status == 407) return makeError(ErrorCode::AuthenticationFailed, code + ": authentication required or refused");
    if (status == 403) return makeError(ErrorCode::PermissionDenied, code + ": forbidden");
    if (status == 404 || status == 410) return makeError(ErrorCode::NotFound, code + ": not found");
    if (status >= 200 && status < 300) return makeError(ErrorCode::NetworkError, code + ": unexpected answer");
    return makeError(ErrorCode::NetworkError, code);
}

bool HttpClient::sendsCredentialsTo(const std::string& target) const {
    auto own = url::split(config_.indexUrl);
    auto other = url::split(target);
    if (!own || !other) return false;
    const auto& a = own.value();
    const auto& b = other.value();
    return a.scheme == b.scheme && lower(a.host) == lower(b.host) &&
           a.port.value_or(defaultPort(a.scheme)) == b.port.value_or(defaultPort(b.scheme));
}

std::string HttpClient::describe() const {
    auto parts = url::split(config_.indexUrl);
    if (!parts) return config_.indexUrl;
    const auto& p = parts.value();
    std::string host = p.host.find(':') != std::string::npos ? "[" + p.host + "]" : p.host;
    std::string out = p.scheme + "://" + (config_.username.empty() ? "" : config_.username + "@") + host;
    if (p.port) out += ":" + std::to_string(*p.port);
    out += p.path;
    if (!p.query.empty()) out += "?" + p.query;
    return out;
}

Status HttpClient::transfer(const std::string& target, std::uint64_t offset, const ChunkSink& sink,
                            const ProgressCallback& progress, const CancellationToken& cancel, bool isIndex) {
    auto parts = url::split(target);
    if (!parts || (parts.value().scheme != "http" && parts.value().scheme != "https")) {
        return makeError(ErrorCode::Unsupported, "not an http(s) URL: " + target);
    }
    if (cancel.isCancelled()) return makeError(ErrorCode::Cancelled, "download cancelled");

    curl::ensureInitialized();
    std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> curl(curl_easy_init(), &curl_easy_cleanup);
    if (!curl) return makeError(ErrorCode::NetworkError, "curl_easy_init failed");
    CURL* handle = curl.get();
    char details[CURL_ERROR_SIZE] = {0};
    const std::string address = url::stripFragment(target);
    TransferState state{handle, &sink, &progress, &cancel, offset, false, std::nullopt};

    curl_easy_setopt(handle, CURLOPT_URL, address.c_str());
    curl_easy_setopt(handle, CURLOPT_NOSIGNAL, 1L);  // required when used from worker threads
    curl_easy_setopt(handle, CURLOPT_ERRORBUFFER, details);
    curl_easy_setopt(handle, CURLOPT_CONNECTTIMEOUT, config_.connectTimeoutSeconds);
    curl_easy_setopt(handle, CURLOPT_USERAGENT, "RetroManager");
#if LIBCURL_VERSION_NUM >= 0x075500
    curl_easy_setopt(handle, CURLOPT_PROTOCOLS_STR, "http,https");
    curl_easy_setopt(handle, CURLOPT_REDIR_PROTOCOLS_STR, "http,https");
#else
    curl_easy_setopt(handle, CURLOPT_PROTOCOLS, static_cast<long>(CURLPROTO_HTTP | CURLPROTO_HTTPS));
    curl_easy_setopt(handle, CURLOPT_REDIR_PROTOCOLS, static_cast<long>(CURLPROTO_HTTP | CURLPROTO_HTTPS));
#endif
    curl_easy_setopt(handle, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(handle, CURLOPT_MAXREDIRS, config_.maxRedirects);
    curl_easy_setopt(handle, CURLOPT_UNRESTRICTED_AUTH, 0L);  // never forward credentials to a redirect target
    if (!config_.username.empty() && sendsCredentialsTo(address)) {
        curl_easy_setopt(handle, CURLOPT_HTTPAUTH, static_cast<long>(CURLAUTH_BASIC));
        curl_easy_setopt(handle, CURLOPT_USERNAME, config_.username.c_str());
        curl_easy_setopt(handle, CURLOPT_PASSWORD, config_.password.c_str());
    }
    if (parts.value().scheme == "https") {
        curl_easy_setopt(handle, CURLOPT_SSL_VERIFYPEER, config_.verifyPeer ? 1L : 0L);
        curl_easy_setopt(handle, CURLOPT_SSL_VERIFYHOST, config_.verifyPeer ? 2L : 0L);
        if (!config_.caBundlePath.empty()) curl_easy_setopt(handle, CURLOPT_CAINFO, config_.caBundlePath.c_str());
    }
    if (isIndex) {
        curl_easy_setopt(handle, CURLOPT_TIMEOUT, config_.transferTimeoutSeconds);
    } else {
        // No whole-transfer timeout: a big ROM legitimately takes minutes.
        curl_easy_setopt(handle, CURLOPT_LOW_SPEED_LIMIT, 1L);
        curl_easy_setopt(handle, CURLOPT_LOW_SPEED_TIME, config_.stallTimeoutSeconds);
    }
    curl_easy_setopt(handle, CURLOPT_BUFFERSIZE, kReceiveBufferBytes);
    curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, &onData);
    curl_easy_setopt(handle, CURLOPT_WRITEDATA, &state);
    curl_easy_setopt(handle, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(handle, CURLOPT_XFERINFOFUNCTION, &onProgress);
    curl_easy_setopt(handle, CURLOPT_XFERINFODATA, &state);
    std::string range;
    if (offset > 0) {
        range = std::to_string(offset) + "-";  // "Range: bytes=<offset>-"
        curl_easy_setopt(handle, CURLOPT_RANGE, range.c_str());
    }

    CURLcode code = curl_easy_perform(handle);
    const std::string where = "http " + address;
    if (state.error) {
        Error error = *state.error;
        error.message = where + ": " + error.message;
        return error;
    }
    if (cancel.isCancelled()) return makeError(ErrorCode::Cancelled, where + ": cancelled");
    if (code != CURLE_OK) return curl::fromCode(code, details, where);
    if (!state.statusChecked) {  // empty body: the status decides (404, 416...)
        long status = 0;
        curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &status);
        if (auto error = errorForStatus(status, offset)) {
            error->message = where + ": " + error->message;
            return *error;
        }
    }
    return success();
}

Result<std::string> HttpClient::fetchIndex() {
    if (Status valid = validate(config_); !valid) return valid.error();
    std::string body;
    bool overflow = false;
    CancellationToken never;
    Status status = transfer(
        config_.indexUrl, 0,
        [&](const char* data, std::size_t size) -> Status {
            if (body.size() + size > config_.maxIndexBytes) {
                overflow = true;
                return makeError(ErrorCode::ParseError, "index larger than " + std::to_string(config_.maxIndexBytes) + " bytes");
            }
            body.append(data, size);
            return success();
        },
        nullptr, never, /*isIndex=*/true);
    if (!status) return status.error();
    return body;
}

Status HttpClient::downloadFile(const std::string& target, const ChunkSink& sink, const ProgressCallback& progress,
                                const CancellationToken& cancel) {
    return transfer(target, 0, sink, progress, cancel, false);
}

Status HttpClient::downloadFileFrom(const std::string& target, std::uint64_t offset, const ChunkSink& sink,
                                    const ProgressCallback& progress, const CancellationToken& cancel) {
    return transfer(target, offset, sink, progress, cancel, false);
}

Result<std::vector<RemoteEntry>> HttpClient::listDirectory(const std::string& target) {
    return makeError(ErrorCode::Unsupported, "HTTP has no directory listing: " + target);
}

Status HttpClient::uploadFile(const std::string& target, const ChunkReader&, std::uint64_t, const ProgressCallback&,
                              const CancellationToken&) {
    return makeError(ErrorCode::Unsupported, "no upload over HTTP: " + target);
}

}  // namespace rm
