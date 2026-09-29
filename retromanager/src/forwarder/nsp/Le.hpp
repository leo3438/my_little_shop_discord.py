#pragma once

// Little-endian field access for the NSP writers and readers.

#include <cstdint>
#include <cstring>
#include <string>

#include "retromanager/forwarder/Sha256.hpp"

namespace rm::nsp::le {

inline void put16(Bytes& b, std::size_t at, std::uint16_t v) {
    for (int i = 0; i < 2; ++i) b[at + i] = static_cast<std::uint8_t>(v >> (8 * i));
}
inline void put32(Bytes& b, std::size_t at, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) b[at + i] = static_cast<std::uint8_t>(v >> (8 * i));
}
inline void put64(Bytes& b, std::size_t at, std::uint64_t v) {
    for (int i = 0; i < 8; ++i) b[at + i] = static_cast<std::uint8_t>(v >> (8 * i));
}
inline void putBytes(Bytes& b, std::size_t at, const void* data, std::size_t size) {
    if (size > 0) std::memcpy(b.data() + at, data, size);
}
inline void putString(Bytes& b, std::size_t at, const std::string& s, std::size_t maxSize) {
    putBytes(b, at, s.data(), std::min(s.size(), maxSize));
}

inline std::uint16_t get16(const Bytes& b, std::size_t at) {
    return static_cast<std::uint16_t>(b[at] | (b[at + 1] << 8));
}
inline std::uint32_t get32(const Bytes& b, std::size_t at) {
    std::uint32_t v = 0;
    for (int i = 0; i < 4; ++i) v |= static_cast<std::uint32_t>(b[at + i]) << (8 * i);
    return v;
}
inline std::uint64_t get64(const Bytes& b, std::size_t at) {
    std::uint64_t v = 0;
    for (int i = 0; i < 8; ++i) v |= static_cast<std::uint64_t>(b[at + i]) << (8 * i);
    return v;
}

inline std::uint64_t alignUp(std::uint64_t v, std::uint64_t alignment) { return (v + alignment - 1) / alignment * alignment; }

}  // namespace rm::nsp::le
