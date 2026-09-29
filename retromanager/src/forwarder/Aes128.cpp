#include "retromanager/forwarder/Aes128.hpp"

#include <algorithm>
#include <cassert>
#include <cstring>

namespace rm::crypto {

namespace {

std::uint8_t xtime(std::uint8_t x) { return static_cast<std::uint8_t>((x << 1) ^ ((x & 0x80) ? 0x1b : 0)); }

std::uint8_t mul(std::uint8_t a, std::uint8_t b) {
    std::uint8_t out = 0;
    while (b) {
        if (b & 1) out ^= a;
        a = xtime(a);
        b >>= 1;
    }
    return out;
}

// S-box and its inverse, derived once from their definition (multiplicative
// inverse in GF(2^8) + affine map) rather than typed in.
struct Tables {
    std::uint8_t sbox[256];
    std::uint8_t inverse[256];
    Tables() {
        for (int x = 0; x < 256; ++x) {
            std::uint8_t inv = 0;
            if (x != 0) {
                for (int y = 1; y < 256; ++y) {
                    if (mul(static_cast<std::uint8_t>(x), static_cast<std::uint8_t>(y)) == 1) {
                        inv = static_cast<std::uint8_t>(y);
                        break;
                    }
                }
            }
            std::uint8_t s = inv;
            for (int i = 1; i < 5; ++i) s ^= static_cast<std::uint8_t>((inv << i) | (inv >> (8 - i)));
            s ^= 0x63;
            sbox[x] = s;
            inverse[s] = static_cast<std::uint8_t>(x);
        }
    }
};

const Tables& tables() {
    static const Tables t;
    return t;
}

void addRoundKey(std::uint8_t s[16], const std::uint8_t* k) {
    for (int i = 0; i < 16; ++i) s[i] ^= k[i];
}

void shiftRows(std::uint8_t s[16], bool inverse) {
    std::uint8_t t[16];
    for (int c = 0; c < 4; ++c) {
        for (int r = 0; r < 4; ++r) {
            int from = inverse ? (c - r + 4) % 4 : (c + r) % 4;
            t[c * 4 + r] = s[from * 4 + r];
        }
    }
    std::memcpy(s, t, 16);
}

void mixColumns(std::uint8_t s[16], bool inverse) {
    for (int c = 0; c < 4; ++c) {
        std::uint8_t* col = s + c * 4;
        std::uint8_t a0 = col[0], a1 = col[1], a2 = col[2], a3 = col[3];
        if (!inverse) {
            col[0] = static_cast<std::uint8_t>(mul(a0, 2) ^ mul(a1, 3) ^ a2 ^ a3);
            col[1] = static_cast<std::uint8_t>(a0 ^ mul(a1, 2) ^ mul(a2, 3) ^ a3);
            col[2] = static_cast<std::uint8_t>(a0 ^ a1 ^ mul(a2, 2) ^ mul(a3, 3));
            col[3] = static_cast<std::uint8_t>(mul(a0, 3) ^ a1 ^ a2 ^ mul(a3, 2));
        } else {
            col[0] = static_cast<std::uint8_t>(mul(a0, 14) ^ mul(a1, 11) ^ mul(a2, 13) ^ mul(a3, 9));
            col[1] = static_cast<std::uint8_t>(mul(a0, 9) ^ mul(a1, 14) ^ mul(a2, 11) ^ mul(a3, 13));
            col[2] = static_cast<std::uint8_t>(mul(a0, 13) ^ mul(a1, 9) ^ mul(a2, 14) ^ mul(a3, 11));
            col[3] = static_cast<std::uint8_t>(mul(a0, 11) ^ mul(a1, 13) ^ mul(a2, 9) ^ mul(a3, 14));
        }
    }
}

// GF(2^128) multiplication by x, little-endian convention of IEEE 1619.
void doubleTweak(std::uint8_t t[16]) {
    std::uint8_t carry = 0;
    for (int i = 0; i < 16; ++i) {
        std::uint8_t next = static_cast<std::uint8_t>(t[i] >> 7);
        t[i] = static_cast<std::uint8_t>((t[i] << 1) | carry);
        carry = next;
    }
    if (carry) t[0] ^= 0x87;
}

void xts(const AesKey& dataKey, const AesKey& tweakKey, std::uint8_t* data, std::size_t size, std::size_t sectorSize,
         std::uint64_t sector, bool encrypt) {
    assert(sectorSize % 16 == 0 && size % sectorSize == 0);
    Aes128 cipher(dataKey);
    Aes128 tweaker(tweakKey);
    for (std::size_t offset = 0; offset < size; offset += sectorSize, ++sector) {
        std::uint8_t tweak[16] = {};
        for (int i = 0; i < 8; ++i) tweak[15 - i] = static_cast<std::uint8_t>(sector >> (8 * i));
        tweaker.encryptBlock(tweak);
        for (std::size_t b = 0; b < sectorSize; b += 16) {
            std::uint8_t* block = data + offset + b;
            for (int i = 0; i < 16; ++i) block[i] ^= tweak[i];
            if (encrypt) {
                cipher.encryptBlock(block);
            } else {
                cipher.decryptBlock(block);
            }
            for (int i = 0; i < 16; ++i) block[i] ^= tweak[i];
            doubleTweak(tweak);
        }
    }
}

}  // namespace

Aes128::Aes128(const AesKey& key) {
    const auto& sbox = tables().sbox;
    std::memcpy(roundKeys_.data(), key.data(), 16);
    std::uint8_t rcon = 1;
    for (int i = 16; i < 176; i += 4) {
        std::uint8_t t[4] = {roundKeys_[i - 4], roundKeys_[i - 3], roundKeys_[i - 2], roundKeys_[i - 1]};
        if (i % 16 == 0) {
            std::uint8_t first = t[0];
            t[0] = static_cast<std::uint8_t>(sbox[t[1]] ^ rcon);
            t[1] = sbox[t[2]];
            t[2] = sbox[t[3]];
            t[3] = sbox[first];
            rcon = xtime(rcon);
        }
        for (int j = 0; j < 4; ++j) roundKeys_[i + j] = static_cast<std::uint8_t>(roundKeys_[i - 16 + j] ^ t[j]);
    }
}

void Aes128::encryptBlock(std::uint8_t s[16]) const {
    const auto& sbox = tables().sbox;
    addRoundKey(s, roundKeys_.data());
    for (int round = 1; round <= 10; ++round) {
        for (int i = 0; i < 16; ++i) s[i] = sbox[s[i]];
        shiftRows(s, false);
        if (round < 10) mixColumns(s, false);
        addRoundKey(s, roundKeys_.data() + round * 16);
    }
}

void Aes128::decryptBlock(std::uint8_t s[16]) const {
    const auto& inverse = tables().inverse;
    addRoundKey(s, roundKeys_.data() + 160);
    for (int round = 9; round >= 0; --round) {
        shiftRows(s, true);
        for (int i = 0; i < 16; ++i) s[i] = inverse[s[i]];
        addRoundKey(s, roundKeys_.data() + round * 16);
        if (round > 0) mixColumns(s, true);
    }
}

void ecbEncrypt(const AesKey& key, std::uint8_t* data, std::size_t size) {
    assert(size % 16 == 0);
    Aes128 aes(key);
    for (std::size_t i = 0; i < size; i += 16) aes.encryptBlock(data + i);
}

void ecbDecrypt(const AesKey& key, std::uint8_t* data, std::size_t size) {
    assert(size % 16 == 0);
    Aes128 aes(key);
    for (std::size_t i = 0; i < size; i += 16) aes.decryptBlock(data + i);
}

void ctrTransform(const AesKey& key, AesBlock& counter, std::uint8_t* data, std::size_t size) {
    Aes128 aes(key);
    for (std::size_t offset = 0; offset < size; offset += 16) {
        AesBlock stream = counter;
        aes.encryptBlock(stream.data());
        std::size_t n = std::min<std::size_t>(16, size - offset);
        for (std::size_t i = 0; i < n; ++i) data[offset + i] ^= stream[i];
        for (int i = 15; i >= 0; --i) {
            if (++counter[static_cast<std::size_t>(i)] != 0) break;
        }
    }
}

AesBlock ncaCounter(std::uint64_t upperIv, std::uint64_t offset) {
    AesBlock counter{};
    std::uint64_t low = offset >> 4;
    for (int i = 0; i < 8; ++i) {
        counter[7 - i] = static_cast<std::uint8_t>(upperIv >> (8 * i));
        counter[15 - i] = static_cast<std::uint8_t>(low >> (8 * i));
    }
    return counter;
}

void xtsEncrypt(const AesKey& dataKey, const AesKey& tweakKey, std::uint8_t* data, std::size_t size,
                std::size_t sectorSize, std::uint64_t firstSector) {
    xts(dataKey, tweakKey, data, size, sectorSize, firstSector, true);
}

void xtsDecrypt(const AesKey& dataKey, const AesKey& tweakKey, std::uint8_t* data, std::size_t size,
                std::size_t sectorSize, std::uint64_t firstSector) {
    xts(dataKey, tweakKey, data, size, sectorSize, firstSector, false);
}

}  // namespace rm::crypto
