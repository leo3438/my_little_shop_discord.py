#include "retromanager/fs/FileInstall.hpp"

#include <algorithm>
#include <vector>

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

namespace {

constexpr std::size_t kPrefixBytes = 4096;  // longest header any check looks at

}  // namespace

FileInstall::FileInstall(IFileSystem& fs, std::string destination, std::unique_ptr<BufferedWriteStream> stream,
                         std::string expectedCrc32)
    : fs_(fs),
      destination_(std::move(destination)),
      stream_(std::move(stream)),
      expectedCrc32_(std::move(expectedCrc32)) {}

Status FileInstall::primeFromPartial(std::uint64_t size) {
    auto partial = fs_.openRead(stagingPath(destination_));
    if (!partial) return partial.error();
    std::vector<char> buffer(64 * 1024);
    std::uint64_t read = 0;
    while (read < size) {
        auto n = partial.value()->read(buffer.data(), buffer.size());
        if (!n) return n.error();
        if (n.value() == 0) break;
        crc_.update(buffer.data(), n.value());
        if (prefix_.size() < kPrefixBytes) {
            prefix_.append(buffer.data(), std::min<std::size_t>(n.value(), kPrefixBytes - prefix_.size()));
        }
        read += n.value();
    }
    if (read != size) return makeError(ErrorCode::IoError, "partial file changed while resuming");
    written_ = resumedFrom_ = size;
    return success();
}

void FileInstall::setHeaderCheck(std::size_t bytes, std::function<Status(std::string_view)> check) {
    headerBytes_ = bytes;
    headerCheck_ = std::move(check);
    headerChecked_ = headerBytes_ == 0 || !headerCheck_;
    header_.clear();
    if (headerChecked_ || resumedFrom_ == 0) return;
    // The partial file already holds (part of) the header.
    header_ = prefix_.substr(0, std::min<std::size_t>(bytes, prefix_.size()));
    if (!checkHeaderIfComplete() && !restart()) finished_ = true;  // garbage partial: start over
}

Status FileInstall::restart() {
    if (finished_) return makeError(ErrorCode::IoError, "install already finished");
    stream_.reset();  // discards the partial staging file
    auto stream = fs_.openWrite(destination_);
    if (!stream) {
        finished_ = true;
        return stream.error();
    }
    stream_ = std::make_unique<BufferedWriteStream>(std::move(stream.value()));
    crc_ = Crc32();
    written_ = resumedFrom_ = 0;
    prefix_.clear();
    header_.clear();
    headerChecked_ = headerBytes_ == 0 || !headerCheck_;
    return success();
}

Status FileInstall::suspend() {
    if (finished_) return makeError(ErrorCode::IoError, "install already finished");
    finished_ = true;
    return stream_->suspend();
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
                                                      const std::string& what, bool resume) {
    std::uint64_t partial = 0;
    if (resume) {
        auto info = fs.stat(stagingPath(destination));
        if (info && info.value().type == EntryType::File && (sizeBytes == 0 || info.value().size <= sizeBytes)) {
            partial = info.value().size;
        }
    }
    SpaceReport space = checkSpace(fs, destination, sizeBytes > partial ? sizeBytes - partial : 0);
    if (!space.sufficient) {
        return makeError(ErrorCode::InsufficientSpace,
                         what + " needs " + formatBytes(sizeBytes) + " (+" + formatBytes(kSpaceMargin) + " margin), only " +
                             formatBytes(space.availableBytes.value_or(0)) + " free on the SD card");
    }
    if (Status dir = fs.createDirectories(vpath::parent(destination)); !dir) return dir.error();

    auto install = std::make_unique<FileInstall>(fs, destination, nullptr, std::move(expectedCrc32));
    if (partial > 0 && install->primeFromPartial(partial)) {
        auto stream = fs.openWrite(destination, WriteOptions{true});
        if (!stream) return stream.error();
        if (stream.value()->resumedFrom() == partial) {
            install->stream_ = std::make_unique<BufferedWriteStream>(std::move(stream.value()));
            return install;
        }
    }
    install = std::make_unique<FileInstall>(fs, destination, nullptr, install->expectedCrc32_);
    auto stream = fs.openWrite(destination);  // fresh: any stale partial file is dropped
    if (!stream) return stream.error();
    install->stream_ = std::make_unique<BufferedWriteStream>(std::move(stream.value()));
    return install;
}

}  // namespace rm
