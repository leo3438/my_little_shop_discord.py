#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace rm {

// Incremental MD5 (RFC 1321). Only used to recognise known files (BIOS
// dumps are identified by their MD5 in the libretro documentation), never
// for security.
class Md5 {
  public:
    Md5();
    void update(const char* data, std::size_t size);
    std::string hex() const;  // 32 lowercase hex digits; the object can keep being updated

    static std::string of(std::string_view data);
    static bool isDigest(std::string_view text);  // 32 hex digits, any case

  private:
    void transform(const unsigned char block[64]);

    std::array<std::uint32_t, 4> state_;
    std::uint64_t length_ = 0;  // bytes
    unsigned char buffer_[64] = {};
};

}  // namespace rm
