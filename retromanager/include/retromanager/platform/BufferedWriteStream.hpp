#pragma once

#include <memory>
#include <vector>

#include "retromanager/platform/IFileSystem.hpp"

namespace rm {

// Coalesces small writes (network chunks are ~16 KB) into large ones before
// they reach the SD card, which is much faster with big sequential writes.
// Memory use is bounded by `capacity`, whatever the file size.
class BufferedWriteStream : public IWriteStream {
  public:
    static constexpr std::size_t kDefaultCapacity = 1024 * 1024;  // 1 MiB

    explicit BufferedWriteStream(std::unique_ptr<IWriteStream> inner, std::size_t capacity = kDefaultCapacity);

    Status write(const char* data, std::size_t size) override;
    Status close() override;    // flushes, then commits the inner stream
    Status suspend() override;  // flushes, then keeps the inner stream's staging file
    std::uint64_t resumedFrom() const override { return inner_->resumedFrom(); }

    std::size_t capacity() const { return capacity_; }

  private:
    Status flush();

    std::unique_ptr<IWriteStream> inner_;
    std::size_t capacity_;
    std::vector<char> buffer_;
    std::size_t used_ = 0;
    bool closed_ = false;
};

}  // namespace rm
