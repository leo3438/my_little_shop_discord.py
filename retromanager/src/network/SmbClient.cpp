#include "retromanager/network/SmbClient.hpp"

#include <fcntl.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

// Third-party C headers (zero-size arrays...): not held to our warning set.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
extern "C" {
#include <smb2/smb2.h>  // first: libsmb2.h uses its types
#include <smb2/libsmb2.h>
#include <smb2/smb2-errors.h>
}
#pragma GCC diagnostic pop

#include "retromanager/core/Url.hpp"
#include "retromanager/platform/VirtualPath.hpp"

namespace rm {

namespace {

std::string lower(std::string value) {
    for (char& c : value) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return value;
}

std::string hostForUrl(const std::string& host) {
    return host.find(':') != std::string::npos ? "[" + host + "]" : host;  // IPv6 literal
}

// A connected session to the configured share, closed on destruction.
class Session {
  public:
    explicit Session(const SmbConfig& config) : config_(config), smb2_(smb2_init_context()) {}
    ~Session() {
        if (smb2_ == nullptr) return;
        if (connected_) smb2_disconnect_share(smb2_);
        smb2_destroy_context(smb2_);
    }
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    Status connect() {
        if (smb2_ == nullptr) return makeError(ErrorCode::NetworkError, "smb2_init_context failed");
        smb2_set_security_mode(smb2_, SMB2_NEGOTIATE_SIGNING_ENABLED);
        smb2_set_timeout(smb2_, config_.timeoutSeconds);
        // Guest: a user name but no password makes libsmb2 authenticate
        // anonymously, which Samba maps to guest on "guest ok" shares.
        const std::string user = config_.username.empty() ? "guest" : config_.username;
        smb2_set_user(smb2_, user.c_str());
        if (!config_.username.empty()) smb2_set_password(smb2_, config_.password.c_str());
        if (!config_.domain.empty()) smb2_set_domain(smb2_, config_.domain.c_str());
        const std::string server = hostForUrl(config_.host) + ":" + std::to_string(config_.port);
        int rc = smb2_connect_share(smb2_, server.c_str(), config_.share.c_str(), user.c_str());
        if (rc < 0) return error(rc, "connect to //" + hostForUrl(config_.host) + ":" + std::to_string(config_.port) + "/" + config_.share);
        connected_ = true;
        return success();
    }

    // libsmb2 result -> Error. The NT status, when there is one, is the real
    // reason (libsmb2's own text is then often just "socket closed", the
    // server's way of ending a refused session): it is named and explained.
    Error error(int rc, const std::string& what) const {
        std::string detail = smb2_ != nullptr && smb2_get_error(smb2_) != nullptr ? smb2_get_error(smb2_) : "";
        while (!detail.empty() && (detail.back() == '\n' || detail.back() == ' ' || detail.back() == '.')) detail.pop_back();
        if (detail.empty()) detail = std::strerror(rc < 0 ? -rc : EIO);
        const std::uint32_t status = smb2_ != nullptr ? static_cast<std::uint32_t>(smb2_get_nterror(smb2_)) : 0;

        struct Known {
            std::uint32_t status;
            const char* name;
            const char* meaning;
            ErrorCode code;
        };
        static const Known kKnown[] = {
            {SMB2_STATUS_LOGON_FAILURE, "STATUS_LOGON_FAILURE", "wrong user name or password", ErrorCode::AuthenticationFailed},
            {SMB2_STATUS_WRONG_PASSWORD, "STATUS_WRONG_PASSWORD", "wrong password", ErrorCode::AuthenticationFailed},
            {SMB2_STATUS_ACCOUNT_DISABLED, "STATUS_ACCOUNT_DISABLED", "this account is disabled", ErrorCode::AuthenticationFailed},
            {SMB2_STATUS_PASSWORD_EXPIRED, "STATUS_PASSWORD_EXPIRED", "the password has expired", ErrorCode::AuthenticationFailed},
            {SMB2_STATUS_ACCESS_DENIED, "STATUS_ACCESS_DENIED", "this user (or guest) may not access it", ErrorCode::PermissionDenied},
            {SMB2_STATUS_BAD_NETWORK_NAME, "STATUS_BAD_NETWORK_NAME", "no share with this name on the server", ErrorCode::NotFound},
            {SMB2_STATUS_OBJECT_NAME_NOT_FOUND, "STATUS_OBJECT_NAME_NOT_FOUND", "no such file", ErrorCode::NotFound},
            {SMB2_STATUS_OBJECT_PATH_NOT_FOUND, "STATUS_OBJECT_PATH_NOT_FOUND", "no such folder", ErrorCode::NotFound},
            {SMB2_STATUS_NO_SUCH_FILE, "STATUS_NO_SUCH_FILE", "no such file", ErrorCode::NotFound},
        };
        for (const Known& known : kKnown) {
            if (known.status != status) continue;
            char hex[16];
            std::snprintf(hex, sizeof hex, "0x%08X", static_cast<unsigned>(status));
            return makeError(known.code, "smb " + what + ": " + known.name + " (" + hex + "): " + known.meaning);
        }
        std::string message = "smb " + what + ": " + detail;
        if (status != 0 && status != SMB2_STATUS_SUCCESS) {
            char hex[16];
            std::snprintf(hex, sizeof hex, "0x%08X", static_cast<unsigned>(status));
            if (message.find(hex) == std::string::npos && message.find(lower(hex)) == std::string::npos) {
                message += std::string(" (NT status ") + hex + ")";
            }
        }
        // No status: the server could not be reached (or does not speak SMB2).
        ErrorCode code = rc == -ENOENT ? ErrorCode::NotFound : ErrorCode::NetworkError;
        if (detail.find("connect failed with 111") != std::string::npos) message += ": connection refused (wrong port, or SMB off)";
        return makeError(code, message);
    }

    smb2_context* get() const { return smb2_; }

  private:
    const SmbConfig& config_;
    smb2_context* smb2_;
    bool connected_ = false;
};

// Share-relative path for libsmb2: no leading '/', '/' separators.
std::string inShare(const std::string& path) { return path.empty() || path.front() != '/' ? path : path.substr(1); }

// An open file, closed on destruction.
struct OpenFile {
    smb2_context* smb2;
    smb2fh* fh;
    ~OpenFile() {
        if (fh != nullptr) smb2_close(smb2, fh);
    }
};

std::uint32_t chunkSize(smb2_context* smb2, bool write) {
    std::uint32_t max = write ? smb2_get_max_write_size(smb2) : smb2_get_max_read_size(smb2);
    if (max == 0) max = 64 * 1024;
    return std::min(max, SmbClient::kMaxChunkBytes);
}

}  // namespace

// ---------------------------------------------------------------------------

Result<SmbConfig> smbConfigFromUrl(const std::string& text, const std::string& username, const std::string& password) {
    if (text.find_first_not_of(" \t\r\n") == std::string::npos) return makeError(ErrorCode::NotConfigured, "no SMB URL");
    auto parts = url::split(url::encodeForTransfer(text));
    if (!parts) return parts.error();
    if (parts.value().scheme != "smb") {
        return makeError(ErrorCode::Unsupported, "not an smb:// URL: " + text);
    }
    SmbConfig config;
    config.host = parts.value().host;
    config.port = parts.value().port.value_or(445);

    std::string path = url::percentDecode(parts.value().path);  // "/Share/dir/file"
    std::size_t shareEnd = path.find('/', 1);
    config.share = path.substr(1, shareEnd == std::string::npos ? std::string::npos : shareEnd - 1);
    if (config.share.empty()) {
        return makeError(ErrorCode::InvalidArgument, "the SMB URL needs a share: smb://host/share/...: " + text);
    }
    config.indexPath = shareEnd == std::string::npos ? "/" : path.substr(shareEnd);
    if (config.indexPath.back() == '/') config.indexPath += "index.json";

    // Dedicated fields win; "[domain;]user[:password]@" in the URL is a fallback.
    std::string info = parts.value().userInfo;
    if (std::size_t semicolon = info.find(';'); semicolon != std::string::npos) {
        config.domain = url::percentDecode(info.substr(0, semicolon));
        info = info.substr(semicolon + 1);
    }
    std::size_t colon = info.find(':');
    std::string urlUser = url::percentDecode(info.substr(0, colon));
    std::string urlPassword = colon == std::string::npos ? "" : url::percentDecode(info.substr(colon + 1));
    config.username = !username.empty() ? username : urlUser;
    config.password = !username.empty() ? password : urlPassword;

    if (Status valid = SmbClient::validate(config); !valid) return valid.error();
    return config;
}

SmbClient::SmbClient(SmbConfig config) : config_(std::move(config)) {}

Status SmbClient::validate(const SmbConfig& config) {
    if (config.host.empty()) return makeError(ErrorCode::InvalidArgument, "SMB host is empty");
    if (config.host.find_first_of("/?#@ \t") != std::string::npos) {
        return makeError(ErrorCode::InvalidArgument, "SMB host must be a bare host name or IP: " + config.host);
    }
    if (config.port == 0) return makeError(ErrorCode::InvalidArgument, "SMB port must be between 1 and 65535");
    if (config.share.empty() || config.share.find_first_of("/\\") != std::string::npos) {
        return makeError(ErrorCode::InvalidArgument, "SMB share must be a single name: \"" + config.share + "\"");
    }
    if (config.indexPath.empty() || config.indexPath.front() != '/') {
        return makeError(ErrorCode::InvalidArgument, "index path must start with '/': " + config.indexPath);
    }
    if (config.maxIndexBytes == 0) return makeError(ErrorCode::InvalidArgument, "maxIndexBytes must be positive");
    return success();
}

std::string SmbClient::buildUrl(const SmbConfig& config, std::string_view path) {
    return "smb://" + hostForUrl(config.host) + ":" + std::to_string(config.port) + "/" +
           url::percentEncodePath(config.share) + url::percentEncodePath(path);
}

std::string SmbClient::describe() const {
    return "smb://" + (config_.username.empty() ? std::string("guest") : config_.username) + "@" +
           hostForUrl(config_.host) + ":" + std::to_string(config_.port) + "/" + config_.share + config_.indexPath;
}

std::string SmbClient::indexUrl() const { return buildUrl(config_, config_.indexPath); }

Result<std::string> SmbClient::pathOnServer(const std::string& target) const {
    auto parts = url::split(url::encodeForTransfer(target));
    if (!parts) return parts.error();
    const url::UrlParts& p = parts.value();
    std::string path = url::percentDecode(p.path);
    std::size_t shareEnd = path.find('/', 1);
    std::string share = path.substr(1, shareEnd == std::string::npos ? std::string::npos : shareEnd - 1);
    bool same = p.scheme == "smb" && p.host == lower(config_.host) && p.port.value_or(445) == config_.port &&
                lower(share) == lower(config_.share);
    if (!same) return makeError(ErrorCode::PermissionDenied, "refusing to send the NAS credentials to another share: " + target);
    auto inside = vpath::normalize(shareEnd == std::string::npos ? "/" : path.substr(shareEnd));
    if (!inside) return inside.error();
    return inside.value();
}

Result<std::string> SmbClient::fetchIndex() {
    std::string text;
    CancellationToken never;
    Status got = downloadFileFrom(
        indexUrl(), 0,
        [&](const char* data, std::size_t size) {
            if (text.size() + size > config_.maxIndexBytes) {
                return Status(makeError(ErrorCode::IoError, "file is larger than " + std::to_string(config_.maxIndexBytes) + " bytes"));
            }
            text.append(data, size);
            return success();
        },
        nullptr, never);
    if (!got) return got.error();
    return text;
}

Status SmbClient::downloadFile(const std::string& url, const ChunkSink& sink, const ProgressCallback& progress,
                               const CancellationToken& cancel) {
    return downloadFileFrom(url, 0, sink, progress, cancel);
}

Status SmbClient::downloadFileFrom(const std::string& url, std::uint64_t offset, const ChunkSink& sink,
                                   const ProgressCallback& progress, const CancellationToken& cancel) {
    auto path = pathOnServer(url);
    if (!path) return path.error();
    if (cancel.isCancelled()) return makeError(ErrorCode::Cancelled, "download cancelled");
    const std::string where = "//" + config_.share + path.value();

    Session session(config_);
    if (Status connected = session.connect(); !connected) return connected;
    smb2_context* smb2 = session.get();
    OpenFile file{smb2, smb2_open(smb2, inShare(path.value()).c_str(), O_RDONLY)};
    if (file.fh == nullptr) return session.error(-ENOENT, "open " + where);

    smb2_stat_64 st{};
    if (int rc = smb2_fstat(smb2, file.fh, &st); rc < 0) return session.error(rc, "stat " + where);
    const std::uint64_t total = st.smb2_size;
    if (offset > total) {
        return makeError(ErrorCode::Unsupported, "smb " + where + ": cannot resume at " + std::to_string(offset) +
                                                     ", the file has " + std::to_string(total) + " bytes");
    }
    if (offset > 0) {
        std::uint64_t now = 0;
        if (smb2_lseek(smb2, file.fh, static_cast<std::int64_t>(offset), SEEK_SET, &now) < 0) {
            return session.error(-EIO, "seek " + where);
        }
    }

    std::vector<std::uint8_t> buffer(chunkSize(smb2, false));
    std::uint64_t received = offset;
    if (progress) progress(TransferProgress{received, total});
    while (received < total) {
        if (cancel.isCancelled()) return makeError(ErrorCode::Cancelled, "smb " + where + ": cancelled");
        std::uint32_t want = static_cast<std::uint32_t>(std::min<std::uint64_t>(buffer.size(), total - received));
        int n = smb2_read(smb2, file.fh, buffer.data(), want);
        if (n < 0) return session.error(n, "read " + where);
        if (n == 0) break;  // the file shrank meanwhile
        if (Status accepted = sink(reinterpret_cast<const char*>(buffer.data()), static_cast<std::size_t>(n)); !accepted) {
            return accepted;
        }
        received += static_cast<std::uint64_t>(n);
        if (progress) progress(TransferProgress{received, total});
    }
    if (received < total) {
        return makeError(ErrorCode::NetworkError, "smb " + where + ": got " + std::to_string(received) + " of " +
                                                      std::to_string(total) + " bytes");
    }
    return success();
}

Result<std::vector<RemoteEntry>> SmbClient::listDirectory(const std::string& url) {
    auto path = pathOnServer(url);
    if (!path) return path.error();
    Session session(config_);
    if (Status connected = session.connect(); !connected) return connected.error();
    smb2_context* smb2 = session.get();
    std::string dir = inShare(path.value());
    while (!dir.empty() && dir.back() == '/') dir.pop_back();
    smb2dir* handle = smb2_opendir(smb2, dir.c_str());
    if (handle == nullptr) {
        Error e = session.error(-ENOENT, "list //" + config_.share + path.value());
        if (e.code == ErrorCode::NetworkError) e.code = ErrorCode::NotFound;
        return e;
    }
    std::vector<RemoteEntry> out;
    while (smb2dirent* entry = smb2_readdir(smb2, handle)) {
        std::string name = entry->name;
        if (name == "." || name == "..") continue;
        if (entry->st.smb2_type != SMB2_TYPE_FILE && entry->st.smb2_type != SMB2_TYPE_DIRECTORY) continue;
        RemoteEntry e;
        e.name = name;
        e.isDirectory = entry->st.smb2_type == SMB2_TYPE_DIRECTORY;
        e.size = entry->st.smb2_size;
        e.modifiedAt = static_cast<std::int64_t>(entry->st.smb2_mtime);
        out.push_back(std::move(e));
    }
    smb2_closedir(smb2, handle);
    return out;
}

Status SmbClient::uploadFile(const std::string& url, const ChunkReader& reader, std::uint64_t size,
                             const ProgressCallback& progress, const CancellationToken& cancel) {
    auto path = pathOnServer(url);
    if (!path) return path.error();
    if (path.value() == "/" || url.back() == '/') return makeError(ErrorCode::InvalidArgument, "upload target is a directory: " + url);
    if (cancel.isCancelled()) return makeError(ErrorCode::Cancelled, "upload cancelled");
    const std::string where = "//" + config_.share + path.value();

    Session session(config_);
    if (Status connected = session.connect(); !connected) return connected;
    smb2_context* smb2 = session.get();

    // Missing parent directories, one level at a time (EEXIST is fine).
    const std::string target = inShare(path.value());
    for (std::size_t slash = target.find('/'); slash != std::string::npos; slash = target.find('/', slash + 1)) {
        smb2_mkdir(smb2, target.substr(0, slash).c_str());
    }
    std::size_t lastSlash = target.rfind('/');
    const std::string folder = lastSlash == std::string::npos ? "" : target.substr(0, lastSlash + 1);
    const std::string temp = folder + "." + target.substr(lastSlash == std::string::npos ? 0 : lastSlash + 1) + ".tmp";

    {
        OpenFile file{smb2, smb2_open(smb2, temp.c_str(), O_WRONLY | O_CREAT | O_TRUNC)};
        if (file.fh == nullptr) return session.error(-EACCES, "create " + where);
        std::vector<char> buffer(chunkSize(smb2, true));
        std::uint64_t sent = 0;
        while (true) {
            if (cancel.isCancelled()) {
                smb2_close(smb2, file.fh);
                file.fh = nullptr;
                smb2_unlink(smb2, temp.c_str());
                return makeError(ErrorCode::Cancelled, "smb " + where + ": cancelled");
            }
            auto n = reader(buffer.data(), buffer.size());
            if (!n) {
                smb2_close(smb2, file.fh);
                file.fh = nullptr;
                smb2_unlink(smb2, temp.c_str());
                return n.error();
            }
            if (n.value() == 0) break;
            std::size_t offset = 0;
            while (offset < n.value()) {
                int written = smb2_write(smb2, file.fh, reinterpret_cast<const std::uint8_t*>(buffer.data() + offset),
                                         static_cast<std::uint32_t>(n.value() - offset));
                if (written <= 0) {
                    Error e = session.error(written == 0 ? -EIO : written, "write " + where);
                    smb2_close(smb2, file.fh);
                    file.fh = nullptr;
                    smb2_unlink(smb2, temp.c_str());
                    return e;
                }
                offset += static_cast<std::size_t>(written);
            }
            sent += n.value();
            if (progress) progress(TransferProgress{sent, size});
        }
        if (sent != size) {
            smb2_close(smb2, file.fh);
            file.fh = nullptr;
            smb2_unlink(smb2, temp.c_str());
            return makeError(ErrorCode::IoError, "smb " + where + ": sent " + std::to_string(sent) + " of " + std::to_string(size) + " bytes");
        }
    }
    // Publish: replace the previous version (SMB rename does not overwrite).
    smb2_unlink(smb2, target.c_str());
    if (int rc = smb2_rename(smb2, temp.c_str(), target.c_str()); rc < 0) {
        Error e = session.error(rc, "rename " + where);
        smb2_unlink(smb2, temp.c_str());
        return e;
    }
    return success();
}

}  // namespace rm
