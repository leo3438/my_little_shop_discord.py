#include "retromanager/network/SourceFactory.hpp"

#include "retromanager/core/Url.hpp"
#include "retromanager/network/MockRemoteSource.hpp"
#ifdef RM_WITH_SMB
#include "retromanager/network/SmbClient.hpp"
#endif

#include <cctype>

namespace rm {

namespace {

[[maybe_unused]] std::string lower(std::string value) {
    for (char& c : value) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return value;
}

}  // namespace

Result<FtpConfig> ftpConfigFromShop(const ShopConfig& shop) {
    if (shop.url.empty()) return makeError(ErrorCode::NotConfigured, "no shop URL in config.json");

    auto parts = url::split(shop.url);
    if (!parts) return parts.error();
    if (parts.value().scheme != "ftp" && parts.value().scheme != "ftps") {
        return makeError(ErrorCode::Unsupported, "unsupported shop URL scheme \"" + parts.value().scheme +
                                                     "\" (use ftp:// or ftps://)");
    }

    FtpConfig ftp;
    ftp.host = parts.value().host;
    ftp.port = parts.value().port.value_or(21);
    ftp.useTls = parts.value().scheme == "ftps";  // explicit FTPS (AUTH TLS), as most NAS offer
    ftp.verifyPeer = shop.verifyTls;

    std::string path = url::percentDecode(parts.value().path);
    if (path.back() == '/') path += "index.json";
    ftp.indexPath = path;

    // Dedicated fields win; "user:password@" in the URL is a fallback.
    const std::string& userInfo = parts.value().userInfo;
    std::size_t colon = userInfo.find(':');
    std::string urlUser = url::percentDecode(userInfo.substr(0, colon));
    std::string urlPassword = colon == std::string::npos ? "" : url::percentDecode(userInfo.substr(colon + 1));
    ftp.username = !shop.username.empty() ? shop.username : (!urlUser.empty() ? urlUser : "anonymous");
    ftp.password = !shop.password.empty() ? shop.password : urlPassword;

    if (Status valid = FtpClient::validate(ftp); !valid) return valid.error();
    return ftp;
}

Result<HttpConfig> httpConfigFromShop(const ShopConfig& shop, const std::string& caBundle) {
    if (shop.url.empty()) return makeError(ErrorCode::NotConfigured, "no shop URL in config.json");
    auto parts = url::split(shop.url);
    if (!parts) return parts.error();
    if (parts.value().scheme != "http" && parts.value().scheme != "https") {
        return makeError(ErrorCode::Unsupported, "a web shop needs an http:// or https:// URL");
    }
    HttpConfig http;
    // Credentials never stay in the URL (they would be logged and displayed).
    const url::UrlParts& p = parts.value();
    std::string host = p.host.find(':') != std::string::npos ? "[" + p.host + "]" : p.host;
    http.indexUrl = p.scheme + "://" + host + (p.port ? ":" + std::to_string(*p.port) : "") + p.path +
                    (p.path.back() == '/' ? "index.json" : "") + (p.query.empty() ? "" : "?" + p.query);
    std::size_t colon = p.userInfo.find(':');
    std::string urlUser = url::percentDecode(p.userInfo.substr(0, colon));
    std::string urlPassword = colon == std::string::npos ? "" : url::percentDecode(p.userInfo.substr(colon + 1));
    http.username = !shop.username.empty() ? shop.username : urlUser;
    http.password = !shop.password.empty() ? shop.password : urlPassword;
    http.verifyPeer = shop.verifyTls;
    http.caBundlePath = caBundle;
    if (Status valid = HttpClient::validate(http); !valid) return valid.error();
    return http;
}

std::unique_ptr<IRemoteSource> createRemoteSource(const ShopConfig& shop, const std::string& caBundle) {
    if (shop.type == "mock") return MockRemoteSource::createDemo();
    if (shop.type == "http") {
        auto http = httpConfigFromShop(shop, caBundle);
        if (!http) {
            return std::make_unique<UnavailableRemoteSource>(makeError(ErrorCode::NotConfigured, http.error().describe()),
                                                             shop.url.empty() ? "(not configured)" : shop.url);
        }
        return std::make_unique<HttpClient>(std::move(http.value()));
    }

    if (shop.type == "smb") {
#ifdef RM_WITH_SMB
        auto smb = smbConfigFromUrl(shop.url, shop.username, shop.password);
        if (!smb) {
            return std::make_unique<UnavailableRemoteSource>(makeError(ErrorCode::NotConfigured, smb.error().describe()),
                                                             shop.url.empty() ? "(not configured)" : shop.url);
        }
        return std::make_unique<SmbClient>(std::move(smb.value()));
#else
        return std::make_unique<UnavailableRemoteSource>(
            makeError(ErrorCode::Unsupported, "this build has no SMB support (RM_WITH_SMB=OFF)"), shop.url);
#endif
    }

    auto ftp = ftpConfigFromShop(shop);
    if (!ftp) {
        // Whatever the reason (empty, invalid, unsupported URL), the fix is
        // the same for the user: edit config.json. The detail is kept.
        return std::make_unique<UnavailableRemoteSource>(makeError(ErrorCode::NotConfigured, ftp.error().describe()),
                                                         shop.url.empty() ? "(not configured)" : shop.url);
    }
    return std::make_unique<FtpClient>(std::move(ftp.value()));
}

std::unique_ptr<SourceRouter> createSourceRouter(const AppConfig& config) {
    HttpConfig publicWeb;  // anonymous, verified: box art from thumbnails.libretro.com...
    publicWeb.indexUrl = config.scraper.baseUrl.empty() ? "https://thumbnails.libretro.com/" : config.scraper.baseUrl;
    publicWeb.caBundlePath = config.caBundle;
    auto router = std::make_unique<SourceRouter>(std::make_shared<HttpClient>(publicWeb));
    for (const ShopConfig& shop : config.sources) {
        router->add(shop.name, shop.type, std::shared_ptr<IRemoteSource>(createRemoteSource(shop, config.caBundle)));
    }
    if (!config.activeSource.empty()) router->setActive(config.activeSource);
    return router;
}

SavesSource createSavesSource(const AppConfig& config) {
    if (config.savesUrl.empty()) {
        if (config.activeShop().type == "mock") {
            auto demo = std::make_unique<MockRemoteSource>("{}", "");
            demo->addDirectory("ftp://mock.local/Saves/");
            return SavesSource{std::move(demo), "ftp://mock.local/Saves/"};
        }
        return SavesSource{std::make_unique<UnavailableRemoteSource>(
                               makeError(ErrorCode::NotConfigured, "no saves_url in config.json"), "(not configured)"),
                           ""};
    }

    auto unusable = [&config](const Error& error) {
        return SavesSource{std::make_unique<UnavailableRemoteSource>(makeError(ErrorCode::NotConfigured, error.describe()),
                                                                     config.savesUrl),
                           config.savesUrl};
    };
    auto parts = url::split(url::encodeForTransfer(config.savesUrl));
    if (!parts) return unusable(parts.error());
    if (parts.value().scheme == "smb") {
#ifdef RM_WITH_SMB
        // Credentials: the URL's, else those of an SMB source on the same
        // server (any share: one NAS account), else guest.
        std::string user, password;
        for (const ShopConfig& shop : config.sources) {
            if (shop.type != "smb" || !parts.value().userInfo.empty()) continue;
            auto own = smbConfigFromUrl(shop.url, shop.username, shop.password);
            if (own && lower(own.value().host) == parts.value().host && own.value().port == parts.value().port.value_or(445)) {
                user = own.value().username;
                password = own.value().password;
                break;
            }
        }
        std::string folder = config.savesUrl;
        if (folder.back() != '/') folder += '/';
        auto smb = smbConfigFromUrl(folder + "index.json", user, password);
        if (!smb) return unusable(smb.error());
        std::string base = SmbClient::buildUrl(smb.value(), smb.value().indexPath.substr(0, smb.value().indexPath.rfind('/') + 1));
        return SavesSource{std::make_unique<SmbClient>(std::move(smb.value())), base};
#else
        return unusable(makeError(ErrorCode::Unsupported, "this build has no SMB support (RM_WITH_SMB=OFF)"));
#endif
    }
    if (parts.value().scheme != "ftp" && parts.value().scheme != "ftps") {
        return unusable(makeError(ErrorCode::Unsupported, "saves_url must be ftp://, ftps:// or smb://"));
    }

    FtpConfig ftp;
    ftp.host = parts.value().host;
    ftp.port = parts.value().port.value_or(21);
    ftp.useTls = parts.value().scheme == "ftps";
    ftp.verifyPeer = config.activeShop().verifyTls;

    const std::string& userInfo = parts.value().userInfo;
    if (!userInfo.empty()) {
        std::size_t colon = userInfo.find(':');
        ftp.username = url::percentDecode(userInfo.substr(0, colon));
        ftp.password = colon == std::string::npos ? "" : url::percentDecode(userInfo.substr(colon + 1));
    } else {
        // The credentials of a shop on the same NAS (host and port), if any.
        for (const ShopConfig& shop : config.sources) {
            auto own = shop.type == "ftp" ? ftpConfigFromShop(shop) : Result<FtpConfig>(makeError(ErrorCode::Unsupported, ""));
            if (own && own.value().host == ftp.host && own.value().port == ftp.port) {
                ftp.username = own.value().username;
                ftp.password = own.value().password;
                ftp.verifyPeer = shop.verifyTls;
                break;
            }
        }
    }  // else: anonymous

    if (Status valid = FtpClient::validate(ftp); !valid) return unusable(valid.error());

    // Canonical folder URL without credentials (they travel as curl options).
    std::string base = parts.value().scheme + "://" + (ftp.host.find(':') != std::string::npos ? "[" + ftp.host + "]" : ftp.host) +
                       ":" + std::to_string(ftp.port) + parts.value().path;
    if (base.back() != '/') base += '/';
    return SavesSource{std::make_unique<FtpClient>(std::move(ftp)), base};
}

}  // namespace rm
