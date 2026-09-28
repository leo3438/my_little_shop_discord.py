#include "retromanager/core/Crc32.hpp"

#include <array>
#include <cstdio>

namespace rm {

namespace {

std::array<std::uint32_t, 256> makeTable() {
    std::array<std::uint32_t, 256> table{};
    for (std::uint32_t i = 0; i < 256; ++i) {
        std::uint32_t c = i;
        for (int k = 0; k < 8; ++k) c = (c & 1u) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        table[i] = c;
    }
    return table;
}

const std::array<std::uint32_t, 256>& table() {
    static const std::array<std::uint32_t, 256> instance = makeTable();
    return instance;
}

}  // namespace

void Crc32::update(const char* data, std::size_t size) {
    const auto& t = table();
    std::uint32_t c = state_;
    for (std::size_t i = 0; i < size; ++i) {
        c = t[(c ^ static_cast<unsigned char>(data[i])) & 0xFFu] ^ (c >> 8);
    }
    state_ = c;
}

std::string Crc32::hex() const {
    char buffer[9];
    std::snprintf(buffer, sizeof(buffer), "%08x", static_cast<unsigned>(value()));
    return buffer;
}

std::uint32_t Crc32::of(std::string_view data) {
    Crc32 crc;
    crc.update(data.data(), data.size());
    return crc.value();
}

}  // namespace rm
