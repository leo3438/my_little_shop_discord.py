#include "retromanager/network/FtpClient.hpp"

#include <curl/curl.h>

#include <memory>
#include <mutex>

#include "retromanager/core/Url.hpp"
#include "retromanager/platform/VirtualPath.hpp"

namespace rm {

namespace {

void ensureCurlInitialized() {
    // curl_global_init is not thread-safe: run it exactly once. It is never
    // paired with curl_global_cleanup: the process exit reclaims everything.
    static std::once_flag once;
    std::call_once(once, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });
}

struct Sink {
    std::string data;
    std::size_t maxBytes;
    bool overflow = false;
};

std::size_t onData(char* chunk, std::size_t size, std::size_t count, void* userData) {
    auto* sink = static_cast<Sink*>(userData);
    std::size_t bytes = size * count;
    if (sink->data.size() + bytes > sink->maxBytes) {
        sink->overflow = true;
        return 0;  // makes curl abort with CURLE_WRITE_ERROR
    }
    sink->data.append(chunk, bytes);
    return bytes;
}

struct StreamState {
    const ChunkSink* sink;
    const ProgressCallback* progress;
    const CancellationToken* cancel;
    Status sinkStatus;
};

std::size_t onStreamData(char* chunk, std::size_t size, std::size_t count, void* userData) {
    auto* state = static_cast<StreamState*>(userData);
    if (state->cancel->isCancelled()) return 0;
    std::size_t bytes = size * count;
    state->sinkStatus = (*state->sink)(chunk, bytes);
    return state->sinkStatus.ok() ? bytes : 0;
}

int onStreamProgress(void* userData, curl_off_t dlTotal, curl_off_t dlNow, curl_off_t, curl_off_t) {
    auto* state = static_cast<StreamState*>(userData);
    if (state->cancel->isCancelled()) return 1;  // -> CURLE_ABORTED_BY_CALLBACK
    if (*state->progress && dlNow > 0) {
        (*state->progress)(TransferProgress{static_cast<std::uint64_t>(dlNow), static_cast<std::uint64_t>(dlTotal)});
    }
    return 0;
}

Error fromCurl(CURLcode code, const char* details, const std::string& where) {
    std::string message = where + ": " + (details[0] != '\0' ? details : curl_easy_strerror(code));
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

// Options shared by every transfer.
void configure(CURL* handle, const FtpConfig& config, const std::string& target, char* details) {
    curl_easy_setopt(handle, CURLOPT_URL, target.c_str());
    curl_easy_setopt(handle, CURLOPT_USERNAME, config.username.c_str());
    curl_easy_setopt(handle, CURLOPT_PASSWORD, config.password.c_str());
    curl_easy_setopt(handle, CURLOPT_CONNECTTIMEOUT, config.connectTimeoutSeconds);
    curl_easy_setopt(handle, CURLOPT_NOSIGNAL, 1L);  // required when used from worker threads
    curl_easy_setopt(handle, CURLOPT_ERRORBUFFER, details);
    if (config.useTls) {
        curl_easy_setopt(handle, CURLOPT_USE_SSL, static_cast<long>(CURLUSESSL_ALL));
        // verifyPeer=false: accept the self-signed certificates of home NAS.
        // The link is still encrypted, but the server is not authenticated.
        curl_easy_setopt(handle, CURLOPT_SSL_VERIFYPEER, config.verifyPeer ? 1L : 0L);
        curl_easy_setopt(handle, CURLOPT_SSL_VERIFYHOST, config.verifyPeer ? 2L : 0L);
    }
}

std::string hostForUrl(const std::string& host) {
    return host.find(':') != std::string::npos ? "[" + host + "]" : host;  // IPv6 literal
}

}  // namespace

FtpClient::FtpClient(FtpConfig config) : config_(std::move(config)) {}

Status FtpClient::validate(const FtpConfig& config) {
    if (config.host.empty()) return makeError(ErrorCode::InvalidArgument, "FTP host is empty");
    if (config.host.find_first_of("/?#@ \t") != std::string::npos) {
        return makeError(ErrorCode::InvalidArgument, "FTP host must be a bare host name or IP: " + config.host);
    }
    if (config.port == 0) return makeError(ErrorCode::InvalidArgument, "FTP port must be between 1 and 65535");
    if (config.indexPath.empty() || config.indexPath.front() != '/') {
        return makeError(ErrorCode::InvalidArgument, "index path must start with '/': " + config.indexPath);
    }
    if (config.maxIndexBytes == 0) return makeError(ErrorCode::InvalidArgument, "maxIndexBytes must be positive");
    return success();
}

Result<std::string> FtpClient::buildUrl(const FtpConfig& config, std::string_view path) {
    if (Status valid = validate(config); !valid) return valid.error();
    if (path.empty() || path.front() != '/') {
        return makeError(ErrorCode::InvalidArgument, "FTP path must start with '/': " + std::string(path));
    }
    // Paths are relative to the login directory (curl semantics), which on a
    // NAS is the share root the user sees in any FTP client.
    return "ftp://" + hostForUrl(config.host) + ":" + std::to_string(config.port) + url::percentEncodePath(path);
}

std::string FtpClient::describe() const {
    std::string scheme = config_.useTls ? "ftps://" : "ftp://";
    return scheme + config_.username + "@" + hostForUrl(config_.host) + ":" + std::to_string(config_.port) +
           config_.indexPath;
}

std::string FtpClient::indexUrl() const {
    auto built = buildUrl(config_, config_.indexPath);
    return built.ok() ? built.value() : std::string();
}

Result<std::string> FtpClient::fetchIndex() { return fetchFile(config_.indexPath, config_.maxIndexBytes); }

Result<std::string> FtpClient::fetchFile(std::string_view path, std::size_t maxBytes) const {
    auto target = buildUrl(config_, path);
    if (!target) return target.error();

    ensureCurlInitialized();
    std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> curl(curl_easy_init(), &curl_easy_cleanup);
    if (!curl) return makeError(ErrorCode::NetworkError, "curl_easy_init failed");

    Sink sink{{}, maxBytes};
    char details[CURL_ERROR_SIZE] = {0};
    CURL* handle = curl.get();
    configure(handle, config_, target.value(), details);
    curl_easy_setopt(handle, CURLOPT_TIMEOUT, config_.transferTimeoutSeconds);
    curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, &onData);
    curl_easy_setopt(handle, CURLOPT_WRITEDATA, &sink);

    CURLcode code = curl_easy_perform(handle);
    std::string where = "ftp " + std::string(path);
    if (sink.overflow) {
        return makeError(ErrorCode::IoError, where + ": file is larger than " + std::to_string(maxBytes) + " bytes");
    }
    if (code != CURLE_OK) return fromCurl(code, details, where);
    return std::move(sink.data);
}

Result<std::string> FtpClient::pathOnServer(const std::string& target) const {
    auto parts = url::split(target);
    if (!parts) return parts.error();
    const url::UrlParts& p = parts.value();
    std::string configuredHost = config_.host;
    for (char& c : configuredHost) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    bool sameServer = (p.scheme == "ftp" || p.scheme == "ftps") && p.host == configuredHost &&
                      p.port.value_or(21) == config_.port;
    if (!sameServer) {
        return makeError(ErrorCode::PermissionDenied,
                         "refusing to send the NAS credentials to another server: " + target);
    }
    // Normalize so "/shop/../x" cannot be used to smuggle odd paths.
    auto path = vpath::normalize(url::percentDecode(p.path));
    if (!path) return path.error();
    return path.value();
}

Status FtpClient::downloadFile(const std::string& target, const ChunkSink& sink, const ProgressCallback& progress,
                               const CancellationToken& cancel) {
    auto path = pathOnServer(target);
    if (!path) return path.error();
    auto curlUrl = buildUrl(config_, path.value());
    if (!curlUrl) return curlUrl.error();
    if (cancel.isCancelled()) return makeError(ErrorCode::Cancelled, "download cancelled");

    ensureCurlInitialized();
    std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> curl(curl_easy_init(), &curl_easy_cleanup);
    if (!curl) return makeError(ErrorCode::NetworkError, "curl_easy_init failed");

    StreamState state{&sink, &progress, &cancel, success()};
    char details[CURL_ERROR_SIZE] = {0};
    CURL* handle = curl.get();
    configure(handle, config_, curlUrl.value(), details);
    // No whole-transfer timeout: a big ROM legitimately takes minutes.
    // Abort only if the link stalls.
    curl_easy_setopt(handle, CURLOPT_LOW_SPEED_LIMIT, 1L);
    curl_easy_setopt(handle, CURLOPT_LOW_SPEED_TIME, config_.stallTimeoutSeconds);
    curl_easy_setopt(handle, CURLOPT_BUFFERSIZE, kReceiveBufferBytes);
    curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, &onStreamData);
    curl_easy_setopt(handle, CURLOPT_WRITEDATA, &state);
    curl_easy_setopt(handle, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(handle, CURLOPT_XFERINFOFUNCTION, &onStreamProgress);
    curl_easy_setopt(handle, CURLOPT_XFERINFODATA, &state);

    CURLcode code = curl_easy_perform(handle);
    std::string where = "ftp " + path.value();
    if (!state.sinkStatus) return state.sinkStatus;  // the SD card refused the data
    if (cancel.isCancelled()) return makeError(ErrorCode::Cancelled, where + ": cancelled");
    if (code != CURLE_OK) return fromCurl(code, details, where);
    return success();
}

}  // namespace rm
