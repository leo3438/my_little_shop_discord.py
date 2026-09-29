#include "retromanager/fs/FileInstall.hpp"

#include <algorithm>

#include "retromanager/core/Format.hpp"
#include "retromanager/platform/VirtualPath.hpp"

namespace rm {

SpaceReport checkSpace(IFileSystem& fs, const std::string& destination, std::uint64_t sizeBytes) {
    SpaceReport report;
    if (sizeBytes == 0) return report;  // unknown size: nothing to compare
    report.requiredBytes = sizeBytes + kSpaceMargin;

    std::string probe = vpath::parent(destination);
    while (probe != "/" && !fs.exists(probe)) probe = vpath::parent(probe);

    auto available = fs.availableSpace(probe);
    if (!available) return report;  // platform cannot tell: do not block
    report.availableBytes = available.value();
    report.sufficient = available.value() >= report.requiredBytes;
    return report;
}

FileInstall::FileInstall(std::string destination, std::unique_ptr<BufferedWriteStream> stream, std::string expectedCrc32)
    : destination_(std::move(destination)), stream_(std::move(stream)), expectedCrc32_(std::move(expectedCrc32)) {}

void FileInstall::setHeaderCheck(std::size_t bytes, std::function<Status(std::string_view)> check) {
    headerBytes_ = bytes;
    headerCheck_ = std::move(check);
    headerChecked_ = headerBytes_ == 0 || !headerCheck_;
    header_.clear();
}

Status FileInstall::checkHeaderIfComplete() {
    if (headerChecked_ || header_.size() < headerBytes_) return success();
    headerChecked_ = true;
    return headerCheck_(header_);
}

Status FileInstall::write(const char* data, std::size_t size) {
    if (finished_) return makeError(ErrorCode::IoError, "install already finished");
    if (!headerChecked_) {
        std::size_t take = std::min(size, headerBytes_ - header_.size());
        header_.append(data, take);
        if (Status header = checkHeaderIfComplete(); !header) return header;
    }
    crc_.update(data, size);
    written_ += size;
    return stream_->write(data, size);
}

Status FileInstall::commit() {
    if (finished_) return makeError(ErrorCode::IoError, "install already finished");
    finished_ = true;
    // Checked before closing the stream: a corrupt download must never
    // replace a good file. The staging file dies with the install.
    if (!headerChecked_) {
        return makeError(ErrorCode::IntegrityError, destination_ + ": only " + std::to_string(written_) +
                                                        " bytes, not a valid file");
    }
    if (!expectedCrc32_.empty() && crc_.hex() != expectedCrc32_) {
        return makeError(ErrorCode::IntegrityError, destination_ + ": CRC32 is " + crc_.hex() + ", the shop announced " +
                                                        expectedCrc32_);
    }
    return stream_->close();
}

Result<std::unique_ptr<FileInstall>> beginFileInstall(IFileSystem& fs, const std::string& destination,
                                                      std::uint64_t sizeBytes, std::string expectedCrc32,
                                                      const std::string& what) {
    SpaceReport space = checkSpace(fs, destination, sizeBytes);
    if (!space.sufficient) {
        return makeError(ErrorCode::InsufficientSpace,
                         what + " needs " + formatBytes(sizeBytes) + " (+" + formatBytes(kSpaceMargin) + " margin), only " +
                             formatBytes(space.availableBytes.value_or(0)) + " free on the SD card");
    }
    if (Status dir = fs.createDirectories(vpath::parent(destination)); !dir) return dir.error();
    auto stream = fs.openWrite(destination);
    if (!stream) return stream.error();
    auto buffered = std::make_unique<BufferedWriteStream>(std::move(stream.value()));
    return std::make_unique<FileInstall>(destination, std::move(buffered), std::move(expectedCrc32));
}

}  // namespace rm
