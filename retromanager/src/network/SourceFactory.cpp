#include "retromanager/network/SourceFactory.hpp"

#include "retromanager/core/Url.hpp"
#include "retromanager/network/MockRemoteSource.hpp"

namespace rm {

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

std::unique_ptr<IRemoteSource> createRemoteSource(const ShopConfig& shop) {
    if (shop.type == "mock") return MockRemoteSource::createDemo();

    auto ftp = ftpConfigFromShop(shop);
    if (!ftp) {
        // Whatever the reason (empty, invalid, unsupported URL), the fix is
        // the same for the user: edit config.json. The detail is kept.
        return std::make_unique<UnavailableRemoteSource>(makeError(ErrorCode::NotConfigured, ftp.error().describe()),
                                                         shop.url.empty() ? "(not configured)" : shop.url);
    }
    return std::make_unique<FtpClient>(std::move(ftp.value()));
}

}  // namespace rm
