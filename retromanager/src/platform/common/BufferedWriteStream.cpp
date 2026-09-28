#include "retromanager/platform/BufferedWriteStream.hpp"

#include <algorithm>
#include <cstring>

namespace rm {

BufferedWriteStream::BufferedWriteStream(std::unique_ptr<IWriteStream> inner, std::size_t capacity)
    : inner_(std::move(inner)), capacity_(std::max<std::size_t>(capacity, 1)), buffer_(capacity_) {}

Status BufferedWriteStream::write(const char* data, std::size_t size) {
    if (closed_) return makeError(ErrorCode::IoError, "stream already closed");
    while (size > 0) {
        std::size_t room = capacity_ - used_;
        std::size_t chunk = std::min(room, size);
        std::memcpy(buffer_.data() + used_, data, chunk);
        used_ += chunk;
        data += chunk;
        size -= chunk;
        if (used_ == capacity_) {
            if (Status flushed = flush(); !flushed) return flushed;
        }
    }
    return success();
}

Status BufferedWriteStream::flush() {
    if (used_ == 0) return success();
    Status written = inner_->write(buffer_.data(), used_);
    used_ = 0;
    return written;
}

Status BufferedWriteStream::close() {
    if (closed_) return makeError(ErrorCode::IoError, "stream already closed");
    closed_ = true;
    if (Status flushed = flush(); !flushed) return flushed;
    return inner_->close();
}

}  // namespace rm
