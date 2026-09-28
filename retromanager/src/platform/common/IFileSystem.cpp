#include "retromanager/platform/IFileSystem.hpp"

namespace rm {

bool IFileSystem::exists(std::string_view path) { return stat(path).ok(); }

bool IFileSystem::isFile(std::string_view path) {
    auto info = stat(path);
    return info.ok() && info.value().type == EntryType::File;
}

bool IFileSystem::isDirectory(std::string_view path) {
    auto info = stat(path);
    return info.ok() && info.value().type == EntryType::Directory;
}

Result<std::string> IFileSystem::readFile(std::string_view path) {
    auto stream = openRead(path);
    if (!stream) return stream.error();

    std::string content;
    char buffer[16 * 1024];
    while (true) {
        auto count = stream.value()->read(buffer, sizeof(buffer));
        if (!count) return count.error();
        if (count.value() == 0) break;
        content.append(buffer, count.value());
    }
    return content;
}

Status IFileSystem::writeFile(std::string_view path, std::string_view data) {
    auto stream = openWrite(path);
    if (!stream) return stream.error();
    if (!data.empty()) {
        Status written = stream.value()->write(data.data(), data.size());
        if (!written) return written;
    }
    return stream.value()->close();
}

}  // namespace rm
