#include "retromanager/network/FtpClient.hpp"

#include <curl/curl.h>

#include <algorithm>
#include <cctype>
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
    std::uint64_t offset = 0;  // resumed downloads: curl counts from here
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
        std::uint64_t total = dlTotal > 0 ? state->offset + static_cast<std::uint64_t>(dlTotal) : 0;
        (*state->progress)(TransferProgress{state->offset + static_cast<std::uint64_t>(dlNow), total});
    }
    return 0;
}

struct UploadState {
    const ChunkReader* reader;
    const ProgressCallback* progress;
    const CancellationToken* cancel;
    std::uint64_t size;
    std::uint64_t sent = 0;
    Status readerStatus;
};

std::size_t onUploadRead(char* buffer, std::size_t size, std::size_t count, void* userData) {
    auto* state = static_cast<UploadState*>(userData);
    if (state->cancel->isCancelled()) return CURL_READFUNC_ABORT;
    auto n = (*state->reader)(buffer, size * count);
    if (!n) {
        state->readerStatus = n.error();
        return CURL_READFUNC_ABORT;
    }
    state->sent += n.value();
    return n.value();
}

int onUploadProgress(void* userData, curl_off_t, curl_off_t, curl_off_t, curl_off_t ulNow) {
    auto* state = static_cast<UploadState*>(userData);
    if (state->cancel->isCancelled()) return 1;
    if (*state->progress && ulNow > 0) (*state->progress)(TransferProgress{static_cast<std::uint64_t>(ulNow), state->size});
    return 0;
}

std::size_t onDiscard(char*, std::size_t size, std::size_t count, void*) { return size * count; }

std::size_t onCollect(char* chunk, std::size_t size, std::size_t count, void* userData) {
    auto* text = static_cast<std::string*>(userData);
    std::size_t bytes = size * count;
    if (text->size() + bytes > 8 * 1024 * 1024) return 0;  // absurd listing: refuse
    text->append(chunk, bytes);
    return bytes;
}

// Days since 1970-01-01 for a proleptic Gregorian date (H. Hinnant's algorithm).
std::int64_t daysFromCivil(std::int64_t y, unsigned m, unsigned d) {
    y -= m <= 2;
    const std::int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<std::int64_t>(doe) - 719468;
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
    return downloadFileFrom(target, 0, sink, progress, cancel);
}

Status FtpClient::downloadFileFrom(const std::string& target, std::uint64_t offset, const ChunkSink& sink,
                                   const ProgressCallback& progress, const CancellationToken& cancel) {
    auto path = pathOnServer(target);
    if (!path) return path.error();
    auto curlUrl = buildUrl(config_, path.value());
    if (!curlUrl) return curlUrl.error();
    if (cancel.isCancelled()) return makeError(ErrorCode::Cancelled, "download cancelled");

    ensureCurlInitialized();
    std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> curl(curl_easy_init(), &curl_easy_cleanup);
    if (!curl) return makeError(ErrorCode::NetworkError, "curl_easy_init failed");

    StreamState state{&sink, &progress, &cancel, success(), offset};
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
    if (offset > 0) curl_easy_setopt(handle, CURLOPT_RESUME_FROM_LARGE, static_cast<curl_off_t>(offset));  // REST

    CURLcode code = curl_easy_perform(handle);
    std::string where = "ftp " + path.value();
    if (offset > 0 && (code == CURLE_FTP_COULDNT_USE_REST || code == CURLE_BAD_DOWNLOAD_RESUME ||
                       code == CURLE_RANGE_ERROR)) {
        return makeError(ErrorCode::Unsupported, where + ": the server refused to resume at " + std::to_string(offset));
    }
    if (!state.sinkStatus) return state.sinkStatus;  // the SD card refused the data
    if (cancel.isCancelled()) return makeError(ErrorCode::Cancelled, where + ": cancelled");
    if (code != CURLE_OK) return fromCurl(code, details, where);
    return success();
}

std::optional<std::int64_t> FtpClient::parseMlsdTime(std::string_view value) {
    if (value.size() < 14) return std::nullopt;
    for (std::size_t i = 0; i < 14; ++i) {
        if (!std::isdigit(static_cast<unsigned char>(value[i]))) return std::nullopt;
    }
    if (value.size() > 14 && value[14] != '.') return std::nullopt;
    auto number = [&](std::size_t at, std::size_t len) {
        int n = 0;
        for (std::size_t i = at; i < at + len; ++i) n = n * 10 + (value[i] - '0');
        return n;
    };
    int year = number(0, 4), month = number(4, 2), day = number(6, 2);
    int hour = number(8, 2), minute = number(10, 2), second = number(12, 2);
    if (month < 1 || month > 12 || day < 1 || day > 31 || hour > 23 || minute > 59 || second > 60) return std::nullopt;
    return daysFromCivil(year, static_cast<unsigned>(month), static_cast<unsigned>(day)) * 86400 + hour * 3600 +
           minute * 60 + second;
}

std::vector<RemoteEntry> FtpClient::parseMlsd(std::string_view listing) {
    std::vector<RemoteEntry> entries;
    std::size_t pos = 0;
    while (pos < listing.size()) {
        std::size_t end = listing.find('\n', pos);
        if (end == std::string_view::npos) end = listing.size();
        std::string_view line = listing.substr(pos, end - pos);
        pos = end + 1;
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);

        // "fact=value;fact=value; name": the first space ends the facts.
        std::size_t space = line.find(' ');
        if (space == std::string_view::npos || space + 1 >= line.size()) continue;
        std::string_view facts = line.substr(0, space);
        RemoteEntry entry;
        entry.name = std::string(line.substr(space + 1));
        std::string type;
        std::size_t f = 0;
        while (f < facts.size()) {
            std::size_t semi = facts.find(';', f);
            if (semi == std::string_view::npos) semi = facts.size();
            std::string_view fact = facts.substr(f, semi - f);
            f = semi + 1;
            std::size_t eq = fact.find('=');
            if (eq == std::string_view::npos) continue;
            std::string key(fact.substr(0, eq));
            std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            std::string_view val = fact.substr(eq + 1);
            if (key == "type") {
                type.assign(val.begin(), val.end());
                std::transform(type.begin(), type.end(), type.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            } else if (key == "size") {
                std::uint64_t n = 0;
                bool digits = !val.empty();
                for (char c : val) {
                    if (!std::isdigit(static_cast<unsigned char>(c))) digits = false;
                    else n = n * 10 + static_cast<std::uint64_t>(c - '0');
                }
                if (digits) entry.size = n;
            } else if (key == "modify") {
                entry.modifiedAt = parseMlsdTime(val);
            }
        }
        if (type == "cdir" || type == "pdir" || entry.name == "." || entry.name == "..") continue;
        if (type == "dir") {
            entry.isDirectory = true;
        } else if (!type.empty() && type != "file") {
            continue;  // symlinks and OS-specific types: not ours to sync
        }
        entries.push_back(std::move(entry));
    }
    return entries;
}

Result<std::vector<RemoteEntry>> FtpClient::listWithMlsd(const std::string& directory) const {
    auto target = buildUrl(config_, directory);
    if (!target) return target.error();
    ensureCurlInitialized();
    std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> curl(curl_easy_init(), &curl_easy_cleanup);
    if (!curl) return makeError(ErrorCode::NetworkError, "curl_easy_init failed");

    std::string listing;
    char details[CURL_ERROR_SIZE] = {0};
    configure(curl.get(), config_, target.value(), details);
    curl_easy_setopt(curl.get(), CURLOPT_TIMEOUT, config_.transferTimeoutSeconds);
    curl_easy_setopt(curl.get(), CURLOPT_CUSTOMREQUEST, "MLSD");
    curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, &onCollect);
    curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &listing);
    CURLcode code = curl_easy_perform(curl.get());
    if (code != CURLE_OK) {
        // CWD into a missing directory is refused with 550, which curl
        // reports as "access denied".
        if (code == CURLE_REMOTE_ACCESS_DENIED) return makeError(ErrorCode::NotFound, "ftp " + directory + ": no such directory");
        return fromCurl(code, details, "ftp MLSD " + directory);
    }
    return parseMlsd(listing);
}

Result<std::vector<RemoteEntry>> FtpClient::listWithNlst(const std::string& directory) const {
    auto target = buildUrl(config_, directory);
    if (!target) return target.error();
    ensureCurlInitialized();

    std::string names;
    {
        std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> curl(curl_easy_init(), &curl_easy_cleanup);
        if (!curl) return makeError(ErrorCode::NetworkError, "curl_easy_init failed");
        char details[CURL_ERROR_SIZE] = {0};
        configure(curl.get(), config_, target.value(), details);
        curl_easy_setopt(curl.get(), CURLOPT_TIMEOUT, config_.transferTimeoutSeconds);
        curl_easy_setopt(curl.get(), CURLOPT_DIRLISTONLY, 1L);  // NLST
        curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, &onCollect);
        curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &names);
        CURLcode code = curl_easy_perform(curl.get());
        if (code == CURLE_REMOTE_ACCESS_DENIED) return makeError(ErrorCode::NotFound, "ftp " + directory + ": no such directory");
        if (code != CURLE_OK) return fromCurl(code, details, "ftp NLST " + directory);
    }

    std::vector<RemoteEntry> entries;
    std::size_t pos = 0;
    while (pos < names.size()) {
        std::size_t end = names.find('\n', pos);
        if (end == std::string::npos) end = names.size();
        std::string name = names.substr(pos, end - pos);
        pos = end + 1;
        if (!name.empty() && name.back() == '\r') name.pop_back();
        if (name.empty() || name == "." || name == "..") continue;
        if (std::size_t slash = name.rfind('/'); slash != std::string::npos) name = name.substr(slash + 1);  // some servers prefix the path

        // SIZE + MDTM through a body-less request. Both fail on directories.
        auto fileUrl = buildUrl(config_, directory + name);
        if (!fileUrl) continue;
        std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> curl(curl_easy_init(), &curl_easy_cleanup);
        if (!curl) return makeError(ErrorCode::NetworkError, "curl_easy_init failed");
        char details[CURL_ERROR_SIZE] = {0};
        configure(curl.get(), config_, fileUrl.value(), details);
        curl_easy_setopt(curl.get(), CURLOPT_TIMEOUT, config_.transferTimeoutSeconds);
        curl_easy_setopt(curl.get(), CURLOPT_NOBODY, 1L);
        curl_easy_setopt(curl.get(), CURLOPT_FILETIME, 1L);
        // With NOBODY, curl hands synthetic "Last-Modified:"/"Content-Length:"
        // lines to the write callback: without one they would go to stdout.
        curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, &onDiscard);
        CURLcode code = curl_easy_perform(curl.get());
        curl_off_t size = -1;
        curl_off_t filetime = -1;
        if (code == CURLE_OK) {
            curl_easy_getinfo(curl.get(), CURLINFO_CONTENT_LENGTH_DOWNLOAD_T, &size);
            curl_easy_getinfo(curl.get(), CURLINFO_FILETIME_T, &filetime);
        }
        RemoteEntry entry;
        entry.name = name;
        if (size < 0) {
            entry.isDirectory = true;  // no size: a directory (or something we cannot read anyway)
        } else {
            entry.size = static_cast<std::uint64_t>(size);
            if (filetime >= 0) entry.modifiedAt = static_cast<std::int64_t>(filetime);
        }
        entries.push_back(std::move(entry));
    }
    return entries;
}

Result<std::vector<RemoteEntry>> FtpClient::listDirectory(const std::string& target) {
    auto path = pathOnServer(target);
    if (!path) return path.error();
    std::string directory = path.value();
    if (directory.back() != '/') directory += '/';

    if (config_.useMlsd) {
        auto listed = listWithMlsd(directory);
        if (listed || listed.error().code == ErrorCode::NotFound) return listed;
        // Anything else: most likely "500 unknown command" from a server
        // without MLSD. Retry the portable way.
    }
    return listWithNlst(directory);
}

Status FtpClient::uploadFile(const std::string& target, const ChunkReader& reader, std::uint64_t size,
                             const ProgressCallback& progress, const CancellationToken& cancel) {
    auto path = pathOnServer(target);
    if (!path) return path.error();
    if (path.value() == "/" || path.value().back() == '/') return makeError(ErrorCode::InvalidArgument, "upload target is a directory: " + target);
    if (cancel.isCancelled()) return makeError(ErrorCode::Cancelled, "upload cancelled");

    const std::string directory = vpath::parent(path.value());
    const std::string name = vpath::filename(path.value());
    const std::string staging = "." + name + ".tmp";
    auto stagingUrl = buildUrl(config_, (directory == "/" ? "/" : directory + "/") + staging);
    if (!stagingUrl) return stagingUrl.error();

    ensureCurlInitialized();
    std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> curl(curl_easy_init(), &curl_easy_cleanup);
    if (!curl) return makeError(ErrorCode::NetworkError, "curl_easy_init failed");

    // Rename once the data is complete. curl has CWD'ed into the target
    // directory for the transfer, so bare names are enough.
    std::unique_ptr<curl_slist, decltype(&curl_slist_free_all)> rename(nullptr, &curl_slist_free_all);
    rename.reset(curl_slist_append(rename.release(), ("RNFR " + staging).c_str()));
    rename.reset(curl_slist_append(rename.release(), ("RNTO " + name).c_str()));

    UploadState state{&reader, &progress, &cancel, size, 0, success()};
    char details[CURL_ERROR_SIZE] = {0};
    CURL* handle = curl.get();
    configure(handle, config_, stagingUrl.value(), details);
    curl_easy_setopt(handle, CURLOPT_UPLOAD, 1L);
    curl_easy_setopt(handle, CURLOPT_INFILESIZE_LARGE, static_cast<curl_off_t>(size));
    curl_easy_setopt(handle, CURLOPT_READFUNCTION, &onUploadRead);
    curl_easy_setopt(handle, CURLOPT_READDATA, &state);
    curl_easy_setopt(handle, CURLOPT_FTP_CREATE_MISSING_DIRS, static_cast<long>(CURLFTP_CREATE_DIR));
    curl_easy_setopt(handle, CURLOPT_POSTQUOTE, rename.get());
    curl_easy_setopt(handle, CURLOPT_LOW_SPEED_LIMIT, 1L);
    curl_easy_setopt(handle, CURLOPT_LOW_SPEED_TIME, config_.stallTimeoutSeconds);
    curl_easy_setopt(handle, CURLOPT_UPLOAD_BUFFERSIZE, 64L * 1024);
    curl_easy_setopt(handle, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(handle, CURLOPT_XFERINFOFUNCTION, &onUploadProgress);
    curl_easy_setopt(handle, CURLOPT_XFERINFODATA, &state);

    CURLcode code = curl_easy_perform(handle);
    std::string where = "ftp upload " + path.value();
    if (!state.readerStatus) return state.readerStatus;
    if (cancel.isCancelled()) return makeError(ErrorCode::Cancelled, where + ": cancelled");
    if (code != CURLE_OK) return fromCurl(code, details, where);
    if (state.sent != size) {
        return makeError(ErrorCode::IoError, where + ": sent " + std::to_string(state.sent) + " of " + std::to_string(size) + " bytes");
    }
    return success();
}

}  // namespace rm
