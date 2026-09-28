#include "retromanager/platform/VirtualPath.hpp"

#include <vector>

namespace rm::vpath {

Result<std::string> normalize(std::string_view path) {
    if (path.empty()) {
        return makeError(ErrorCode::InvalidPath, "empty path");
    }
    if (path.find('\0') != std::string_view::npos) {
        return makeError(ErrorCode::InvalidPath, "path contains a NUL byte");
    }
    if (path.front() != '/' && path.front() != '\\') {
        return makeError(ErrorCode::InvalidPath, "path must be absolute: " + std::string(path));
    }

    std::vector<std::string_view> segments;
    std::size_t pos = 0;
    while (pos <= path.size()) {
        std::size_t end = path.find_first_of("/\\", pos);
        if (end == std::string_view::npos) end = path.size();
        std::string_view segment = path.substr(pos, end - pos);
        pos = end + 1;

        if (segment.empty() || segment == ".") continue;
        if (segment == "..") {
            if (segments.empty()) {
                return makeError(ErrorCode::InvalidPath, "path escapes the root: " + std::string(path));
            }
            segments.pop_back();
            continue;
        }
        segments.push_back(segment);
    }

    if (segments.empty()) return std::string("/");

    std::string out;
    for (std::string_view segment : segments) {
        out += '/';
        out += segment;
    }
    return out;
}

Result<std::string> join(std::string_view base, std::string_view child) {
    std::string combined(base);
    combined += '/';
    combined += child;
    return normalize(combined);
}

std::string parent(std::string_view path) {
    std::size_t slash = path.rfind('/');
    if (slash == std::string_view::npos || slash == 0) return "/";
    return std::string(path.substr(0, slash));
}

std::string filename(std::string_view path) {
    std::size_t slash = path.rfind('/');
    if (slash == std::string_view::npos) return std::string(path);
    return std::string(path.substr(slash + 1));
}

std::string stem(std::string_view path) {
    std::string name = filename(path);
    std::size_t dot = name.rfind('.');
    if (dot == std::string::npos || dot == 0) return name;
    return name.substr(0, dot);
}

std::string extension(std::string_view path) {
    std::string name = filename(path);
    std::size_t dot = name.rfind('.');
    if (dot == std::string::npos || dot == 0) return "";
    return name.substr(dot);
}

bool isWithin(std::string_view path, std::string_view ancestor) {
    if (ancestor == "/") return !path.empty() && path.front() == '/';
    if (path.size() < ancestor.size() || path.substr(0, ancestor.size()) != ancestor) return false;
    return path.size() == ancestor.size() || path[ancestor.size()] == '/';
}

}  // namespace rm::vpath
