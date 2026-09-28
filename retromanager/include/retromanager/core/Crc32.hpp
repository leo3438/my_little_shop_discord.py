#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace rm {

// Incremental CRC-32 (IEEE 802.3, the one used by zip and No-Intro DATs),
// so a streamed download can be verified without keeping it in memory.
class Crc32 {
  public:
    void update(const char* data, std::size_t size);
    std::uint32_t value() const { return ~state_; }
    std::string hex() const;  // 8 lowercase hex digits

    static std::uint32_t of(std::string_view data);

  private:
    std::uint32_t state_ = 0xFFFFFFFFu;
};

}  // namespace rm
