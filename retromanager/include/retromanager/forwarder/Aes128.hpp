#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "retromanager/forwarder/Sha256.hpp"

namespace rm::crypto {

using AesKey = std::array<std::uint8_t, 16>;
using AesBlock = std::array<std::uint8_t, 16>;

// Straightforward byte-oriented AES-128 (FIPS 197). Only used to package forwarders
// (NCA headers, key areas, section contents): speed matters little, the
// NCAs are a few hundred KB.
class Aes128 {
  public:
    explicit Aes128(const AesKey& key);
    void encryptBlock(std::uint8_t block[16]) const;
    void decryptBlock(std::uint8_t block[16]) const;

  private:
    std::array<std::uint8_t, 176> roundKeys_;
};

// ECB over whole blocks (size must be a multiple of 16).
void ecbEncrypt(const AesKey& key, std::uint8_t* data, std::size_t size);
void ecbDecrypt(const AesKey& key, std::uint8_t* data, std::size_t size);

// CTR, in place; `counter` is a 128-bit big-endian number, advanced past
// the processed data (a trailing partial block counts as one).
void ctrTransform(const AesKey& key, AesBlock& counter, std::uint8_t* data, std::size_t size);

// The counter of an NCA section at an absolute NCA offset (multiple of 16):
// upper 8 bytes = the section's upper IV, lower 8 = offset / 16, both big-endian.
AesBlock ncaCounter(std::uint64_t upperIv, std::uint64_t offset);

// XTS as used by Nintendo for NCA headers: the sector number that forms the
// tweak is big-endian (IEEE 1619 uses little-endian). `size` must be a
// multiple of `sectorSize`, itself a multiple of 16.
void xtsEncrypt(const AesKey& dataKey, const AesKey& tweakKey, std::uint8_t* data, std::size_t size,
                std::size_t sectorSize, std::uint64_t firstSector);
void xtsDecrypt(const AesKey& dataKey, const AesKey& tweakKey, std::uint8_t* data, std::size_t size,
                std::size_t sectorSize, std::uint64_t firstSector);

}  // namespace rm::crypto
