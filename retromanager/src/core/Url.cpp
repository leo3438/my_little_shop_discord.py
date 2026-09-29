#include "retromanager/core/Url.hpp"

#include <cstring>

#include <cctype>
#include <vector>

namespace rm::url {

namespace {

int hexValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool isUnreserved(unsigned char c) { return std::isalnum(c) || c == '-' || c == '.' || c == '_' || c == '~'; }

// Position right after "scheme://authority" (or after "scheme:" when there
// is no authority). npos when `url` has no scheme.
std::size_t pathStart(std::string_view url) {
    if (!hasScheme(url)) return std::string_view::npos;
    std::size_t colon = url.find(':');
    if (url.substr(colon + 1, 2) != "//") return colon + 1;
    std::size_t end = url.find_first_of("/?#", colon + 3);
    return end == std::string_view::npos ? url.size() : end;
}

// RFC 3986 section 5.2.4, on a path that starts with '/'.
std::string removeDotSegments(std::string_view path) {
    std::vector<std::string_view> segments;
    bool trailingSlash = false;
    std::size_t pos = 1;
    while (pos <= path.size()) {
        std::size_t end = path.find('/', pos);
        bool last = end == std::string_view::npos;
        if (last) end = path.size();
        std::string_view segment = path.substr(pos, end - pos);
        pos = end + 1;

        // A path ending in "/", "/." or "/.." designates a directory.
        trailingSlash = last && (segment.empty() || segment == "." || segment == "..");
        if (segment == ".." && !segments.empty()) segments.pop_back();
        if (segment == "." || segment == ".." || (last && segment.empty())) continue;
        segments.push_back(segment);
    }
    std::string out;
    for (std::string_view segment : segments) {
        out += '/';
        out += segment;
    }
    if (out.empty() || trailingSlash) out += '/';
    return out;
}

// Splits "path?query" so dot removal only touches the path.
std::string normalizePathAndQuery(std::string_view pathAndQuery) {
    std::size_t query = pathAndQuery.find('?');
    std::string_view path = pathAndQuery.substr(0, query);
    std::string out = path.empty() ? std::string("/") : removeDotSegments(path);
    if (query != std::string_view::npos) out += pathAndQuery.substr(query);
    return out;
}

}  // namespace

Result<UrlParts> split(std::string_view url) {
    auto invalid = [&url](const std::string& why) {
        return makeError(ErrorCode::InvalidArgument, why + ": \"" + std::string(url) + "\"");
    };
    if (!hasScheme(url)) return invalid("URL has no scheme");
    std::size_t colon = url.find(':');
    if (url.substr(colon + 1, 2) != "//") return invalid("URL has no \"//host\" part");

    UrlParts parts;
    for (char c : url.substr(0, colon)) parts.scheme += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    std::string_view rest = url.substr(colon + 3);
    std::size_t authorityEnd = rest.find_first_of("/?#");
    std::string_view authority = rest.substr(0, authorityEnd);
    rest = authorityEnd == std::string_view::npos ? std::string_view() : rest.substr(authorityEnd);

    if (std::size_t at = authority.rfind('@'); at != std::string_view::npos) {
        parts.userInfo = std::string(authority.substr(0, at));
        authority = authority.substr(at + 1);
    }

    std::string_view portText;
    if (!authority.empty() && authority.front() == '[') {
        std::size_t close = authority.find(']');
        if (close == std::string_view::npos) return invalid("unterminated IPv6 address");
        parts.host = std::string(authority.substr(1, close - 1));
        std::string_view after = authority.substr(close + 1);
        if (!after.empty()) {
            if (after.front() != ':') return invalid("garbage after IPv6 address");
            portText = after.substr(1);
            if (portText.empty()) return invalid("empty port");
        }
    } else {
        std::size_t portColon = authority.rfind(':');
        parts.host = std::string(authority.substr(0, portColon));
        if (portColon != std::string_view::npos) {
            portText = authority.substr(portColon + 1);
            if (portText.empty()) return invalid("empty port");
        }
    }
    if (parts.host.empty()) return invalid("URL has no host");
    for (char& c : parts.host) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    if (!portText.empty()) {
        if (portText.size() > 5) return invalid("invalid port");
        unsigned long port = 0;
        for (char c : portText) {
            if (!std::isdigit(static_cast<unsigned char>(c))) return invalid("invalid port");
            port = port * 10 + static_cast<unsigned long>(c - '0');
        }
        if (port == 0 || port > 65535) return invalid("port out of range");
        parts.port = static_cast<std::uint16_t>(port);
    }

    std::size_t hash = rest.find('#');
    if (hash != std::string_view::npos) {
        parts.fragment = std::string(rest.substr(hash + 1));
        rest = rest.substr(0, hash);
    }
    std::size_t question = rest.find('?');
    if (question != std::string_view::npos) {
        parts.query = std::string(rest.substr(question + 1));
        rest = rest.substr(0, question);
    }
    parts.path = rest.empty() ? std::string("/") : std::string(rest);
    return parts;
}

bool hasScheme(std::string_view url) {
    std::size_t colon = url.find(':');
    if (colon == std::string_view::npos || colon == 0) return false;
    if (!std::isalpha(static_cast<unsigned char>(url[0]))) return false;
    for (std::size_t i = 1; i < colon; ++i) {
        unsigned char c = static_cast<unsigned char>(url[i]);
        if (!std::isalnum(c) && c != '+' && c != '-' && c != '.') return false;
    }
    return true;
}

std::string percentDecode(std::string_view value) {
    std::string out;
    out.reserve(value.size());
    for (std::size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '%' && i + 2 < value.size()) {
            int high = hexValue(value[i + 1]);
            int low = hexValue(value[i + 2]);
            if (high >= 0 && low >= 0) {
                out += static_cast<char>(high * 16 + low);
                i += 2;
                continue;
            }
        }
        out += value[i];
    }
    return out;
}

std::string percentEncodePath(std::string_view path) {
    static const char* kHex = "0123456789ABCDEF";
    std::string out;
    out.reserve(path.size());
    for (unsigned char c : path) {
        if (isUnreserved(c) || c == '/') {
            out += static_cast<char>(c);
        } else {
            out += '%';
            out += kHex[c >> 4];
            out += kHex[c & 0x0F];
        }
    }
    return out;
}

std::string encodeForTransfer(std::string_view url) {
    static const char* kHex = "0123456789ABCDEF";
    auto isHex = [](char c) { return std::isxdigit(static_cast<unsigned char>(c)) != 0; };
    std::size_t begin = url.find_first_not_of(" \t\r\n");
    if (begin == std::string_view::npos) return "";
    url = url.substr(begin, url.find_last_not_of(" \t\r\n") - begin + 1);
    std::string out;
    out.reserve(url.size());
    for (std::size_t i = 0; i < url.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(url[i]);
        bool escape = c <= 0x20 || c >= 0x7F || std::strchr("\"<>{}|\\^`", c) != nullptr;
        if (c == '%') escape = !(i + 2 < url.size() && isHex(url[i + 1]) && isHex(url[i + 2]));
        if (escape) {
            out += '%';
            out += kHex[c >> 4];
            out += kHex[c & 0x0F];
        } else {
            out += static_cast<char>(c);
        }
    }
    return out;
}

Result<std::string> resolve(std::string_view base, std::string_view reference) {
    if (hasScheme(reference)) return std::string(reference);

    std::size_t basePath = pathStart(base);
    if (basePath == std::string_view::npos) {
        return makeError(ErrorCode::InvalidArgument,
                         "relative URL without an absolute base: " + std::string(reference));
    }

    // Split off the reference's fragment: it never takes part in resolution.
    std::size_t hash = reference.find('#');
    std::string_view refFragment = hash == std::string_view::npos ? std::string_view() : reference.substr(hash);
    std::string_view ref = reference.substr(0, hash);

    std::string resolved;
    if (ref.substr(0, 2) == "//") {
        std::string_view scheme = base.substr(0, base.find(':') + 1);
        std::size_t authorityEnd = ref.find_first_of("/?", 2);
        std::string_view authority = ref.substr(0, authorityEnd);
        std::string_view rest = authorityEnd == std::string_view::npos ? std::string_view() : ref.substr(authorityEnd);
        resolved = std::string(scheme) + std::string(authority) + normalizePathAndQuery(rest);
    } else {
        std::string_view origin = base.substr(0, basePath);
        std::string_view baseRest = base.substr(basePath);
        std::string_view baseDirPath = baseRest.substr(0, baseRest.find_first_of("?#"));

        std::string merged;
        if (ref.empty()) {
            merged = std::string(baseRest.substr(0, baseRest.find('#')));
        } else if (ref.front() == '/') {
            merged = std::string(ref);
        } else if (ref.front() == '?') {
            merged = std::string(baseDirPath) + std::string(ref);
        } else {
            std::size_t slash = baseDirPath.rfind('/');
            std::string dir = slash == std::string_view::npos ? std::string("/") : std::string(baseDirPath.substr(0, slash + 1));
            merged = dir + std::string(ref);
        }
        resolved = std::string(origin) + normalizePathAndQuery(merged);
    }
    resolved += refFragment;
    return resolved;
}

std::string fragment(std::string_view url) {
    std::size_t hash = url.find('#');
    return hash == std::string_view::npos ? std::string() : std::string(url.substr(hash + 1));
}

std::string stripFragment(std::string_view url) { return std::string(url.substr(0, url.find('#'))); }

std::string lastPathSegment(std::string_view url) {
    std::string_view path = url.substr(0, url.find_first_of("?#"));
    std::size_t start = pathStart(path);
    if (start != std::string_view::npos) path = path.substr(start);
    std::size_t slash = path.rfind('/');
    std::string_view segment = slash == std::string_view::npos ? path : path.substr(slash + 1);
    return percentDecode(segment);
}

}  // namespace rm::url
