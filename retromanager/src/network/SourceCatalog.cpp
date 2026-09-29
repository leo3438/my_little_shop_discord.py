#include "retromanager/network/SourceCatalog.hpp"

#include <algorithm>
#include <cctype>

#include "retromanager/core/Url.hpp"
#ifdef RM_WITH_SMB
#include "retromanager/network/SmbClient.hpp"
#endif

namespace rm {

namespace {

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

std::string trim(const std::string& text) {
    std::size_t begin = text.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) return "";
    return text.substr(begin, text.find_last_not_of(" \t\r\n") - begin + 1);
}

}  // namespace

SourceCatalog::SourceCatalog(AppConfig& config, SourceRouter& router, ConfigManager& manager, bool configLoaded)
    : config_(config), router_(router), manager_(manager), writable_(configLoaded) {}

Status SourceCatalog::save() { return manager_.save(config_); }

std::string SourceCatalog::typeForUrl(const std::string& url) {
    std::string text = trim(url);
    std::size_t colon = text.find("://");
    if (colon == std::string::npos) return "";
    std::string scheme = lower(text.substr(0, colon));
    if (scheme == "ftp" || scheme == "ftps") return "ftp";
    if (scheme == "http" || scheme == "https") return "http";
    if (scheme == "smb") return "smb";
    return "";
}

Status SourceCatalog::prepare(ShopConfig& source, const std::string& replacing) const {
    source.name = trim(source.name);
    source.url = trim(source.url);
    source.username = trim(source.username);
    if (!writable_) return makeError(ErrorCode::PermissionDenied, "config.json is invalid: fix it first");
    if (source.name.empty()) return makeError(ErrorCode::InvalidArgument, "a source needs a name");
    for (const ShopConfig& other : config_.sources) {
        if (lower(other.name) == lower(source.name) && lower(other.name) != lower(replacing)) {
            return makeError(ErrorCode::InvalidArgument, "a source is already named \"" + other.name + "\"");
        }
    }
    if (source.type == "mock") return success();
    auto parts = url::split(url::encodeForTransfer(source.url));
    if (!parts) return makeError(ErrorCode::InvalidArgument, "not a URL: " + source.url);
    const std::string scheme = parts.value().scheme;
    if (source.type.empty()) {
        if (scheme == "http" || scheme == "https") source.type = "http";
        else if (scheme == "ftp" || scheme == "ftps") source.type = "ftp";
        else if (scheme == "smb") source.type = "smb";
        else return makeError(ErrorCode::Unsupported, "use an ftp://, ftps://, smb://, http:// or https:// address");
        source.verifyTls = source.type == "http";
    }
    // Same checks the clients will make: refuse now rather than at the first download.
    if (source.type == "http") {
        if (auto http = httpConfigFromShop(source, config_.caBundle); !http) return http.error();
    } else if (source.type == "ftp") {
        if (auto ftp = ftpConfigFromShop(source); !ftp) return ftp.error();
    } else if (source.type == "smb") {
#ifdef RM_WITH_SMB
        if (auto smb = smbConfigFromUrl(source.url, source.username, source.password); !smb) return smb.error();
#else
        return makeError(ErrorCode::Unsupported, "this build has no SMB support (RM_WITH_SMB=OFF)");
#endif
    } else {
        return makeError(ErrorCode::Unsupported, "unknown source type " + source.type);
    }
    return success();
}

Status SourceCatalog::add(ShopConfig source) {
    if (Status ok = prepare(source, ""); !ok) return ok;
    AppConfig before = config_;
    config_.sources.push_back(source);
    if (Status saved = save(); !saved) {
        config_ = before;
        return saved;
    }
    router_.add(source.name, source.type, std::shared_ptr<IRemoteSource>(createRemoteSource(source, config_.caBundle)));
    return success();
}

Status SourceCatalog::update(const std::string& name, ShopConfig source) {
    auto it = std::find_if(config_.sources.begin(), config_.sources.end(),
                           [&](const ShopConfig& s) { return lower(s.name) == lower(name); });
    if (it == config_.sources.end()) return makeError(ErrorCode::NotFound, "no source named " + name);
    const std::string previous = it->name;
    if (Status ok = prepare(source, previous); !ok) return ok;

    AppConfig before = config_;
    *it = source;  // same place in the list
    if (lower(config_.activeSource) == lower(previous)) config_.activeSource = source.name;
    if (Status saved = save(); !saved) {
        config_ = before;
        return saved;
    }
    // A new client: transfers already running keep the old one to the end.
    router_.replace(previous, source.name, source.type,
                    std::shared_ptr<IRemoteSource>(createRemoteSource(source, config_.caBundle)));
    return success();
}

Status SourceCatalog::remove(const std::string& name) {
    if (!writable_) return makeError(ErrorCode::PermissionDenied, "config.json is invalid: fix it first");
    auto it = std::find_if(config_.sources.begin(), config_.sources.end(),
                           [&](const ShopConfig& s) { return lower(s.name) == lower(name); });
    if (it == config_.sources.end()) return makeError(ErrorCode::NotFound, "no source named " + name);
    AppConfig before = config_;
    const std::string removed = it->name;
    config_.sources.erase(it);
    if (lower(config_.activeSource) == lower(removed)) {
        config_.activeSource = config_.sources.empty() ? "" : config_.sources.front().name;
    }
    if (Status saved = save(); !saved) {
        config_ = before;
        return saved;
    }
    router_.remove(removed);
    if (!config_.activeSource.empty()) router_.setActive(config_.activeSource);
    return success();
}

Status SourceCatalog::activate(const std::string& name) {
    if (!writable_) return makeError(ErrorCode::PermissionDenied, "config.json is invalid: fix it first");
    for (const ShopConfig& source : config_.sources) {
        if (lower(source.name) != lower(name)) continue;
        std::string before = config_.activeSource;
        config_.activeSource = source.name;
        if (Status saved = save(); !saved) {
            config_.activeSource = before;
            return saved;
        }
        router_.setActive(source.name);
        return success();
    }
    return makeError(ErrorCode::NotFound, "no source named " + name);
}

}  // namespace rm
