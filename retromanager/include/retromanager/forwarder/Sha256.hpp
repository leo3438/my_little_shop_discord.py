#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace rm {

using Bytes = std::vector<std::uint8_t>;

namespace crypto {

using Sha256Digest = std::array<std::uint8_t, 32>;

// Incremental SHA-256 (FIPS 180-4): the hash of every NCA table, header and
// content id of a generated NSP.
class Sha256 {
  public:
    Sha256();
    void update(const void* data, std::size_t size);
    Sha256Digest digest() const;  // the object can keep being updated

    static Sha256Digest of(const void* data, std::size_t size);
    static Sha256Digest of(const Bytes& data) { return of(data.data(), data.size()); }

  private:
    void transform(const std::uint8_t block[64]);

    std::array<std::uint32_t, 8> state_;
    std::uint64_t length_ = 0;  // bytes
    std::uint8_t buffer_[64] = {};
};

// Lowercase hex of any byte range.
std::string toHex(const std::uint8_t* data, std::size_t size);
template <std::size_t N>
std::string toHex(const std::array<std::uint8_t, N>& data) {
    return toHex(data.data(), N);
}

}  // namespace crypto
}  // namespace rm
