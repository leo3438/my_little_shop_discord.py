#include <gtest/gtest.h>

#include <string>

#include "retromanager/forwarder/Aes128.hpp"
#include "retromanager/forwarder/Sha256.hpp"

using namespace rm;
using namespace rm::crypto;

namespace {

Bytes unhex(const std::string& hex) {
    Bytes out;
    for (std::size_t i = 0; i + 1 < hex.size(); i += 2) out.push_back(static_cast<std::uint8_t>(std::stoi(hex.substr(i, 2), nullptr, 16)));
    return out;
}

std::string hex(const std::uint8_t* data, std::size_t size) {
    static const char* digits = "0123456789abcdef";
    std::string out;
    for (std::size_t i = 0; i < size; ++i) {
        out += digits[data[i] >> 4];
        out += digits[data[i] & 15];
    }
    return out;
}
std::string hex(const Bytes& b) { return hex(b.data(), b.size()); }

AesKey keyFrom(const std::string& text) {
    AesKey key{};
    Bytes b = unhex(text);
    std::copy(b.begin(), b.end(), key.begin());
    return key;
}

}  // namespace

TEST(Sha256Test, KnownAnswers) {
    EXPECT_EQ(toHex(Sha256::of(Bytes{})), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    std::string abc = "abc";
    EXPECT_EQ(toHex(Sha256::of(abc.data(), abc.size())),
              "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    std::string two = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";  // 2 blocks of padding
    EXPECT_EQ(toHex(Sha256::of(two.data(), two.size())),
              "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
}

TEST(Sha256Test, IncrementalMatchesOneShot) {
    Bytes million(1000000, 'a');
    Sha256 h;
    for (std::size_t i = 0; i < million.size(); i += 777) h.update(million.data() + i, std::min<std::size_t>(777, million.size() - i));
    EXPECT_EQ(toHex(h.digest()), "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

TEST(Aes128Test, Fips197Block) {
    Aes128 aes(keyFrom("000102030405060708090a0b0c0d0e0f"));
    Bytes block = unhex("00112233445566778899aabbccddeeff");
    aes.encryptBlock(block.data());
    EXPECT_EQ(hex(block), "69c4e0d86a7b0430d8cdb78070b4c55a");
    aes.decryptBlock(block.data());
    EXPECT_EQ(hex(block), "00112233445566778899aabbccddeeff");
}

TEST(Aes128Test, EcbSp800_38A) {
    AesKey key = keyFrom("2b7e151628aed2a6abf7158809cf4f3c");
    Bytes data = unhex("6bc1bee22e409f96e93d7e117393172aae2d8a571e03ac9c9eb76fac45af8e51");
    ecbEncrypt(key, data.data(), data.size());
    EXPECT_EQ(hex(data), "3ad77bb40d7a3660a89ecaf32466ef97f5d3d58503b9699de785895a96fdbaaf");
    ecbDecrypt(key, data.data(), data.size());
    EXPECT_EQ(hex(data), "6bc1bee22e409f96e93d7e117393172aae2d8a571e03ac9c9eb76fac45af8e51");
}

TEST(Aes128Test, CtrSp800_38A) {  // F.5.1
    AesKey key = keyFrom("2b7e151628aed2a6abf7158809cf4f3c");
    Bytes iv = unhex("f0f1f2f3f4f5f6f7f8f9fafbfcfdfeff");
    Bytes data = unhex("6bc1bee22e409f96e93d7e117393172aae2d8a571e03ac9c9eb76fac45af8e51");
    AesBlock counter{};
    std::copy(iv.begin(), iv.end(), counter.begin());
    ctrTransform(key, counter, data.data(), data.size());
    EXPECT_EQ(hex(data), "874d6191b620e3261bef6864990db6ce9806f66b7970fdff8617187bb9fffdff");
}

TEST(Aes128Test, CtrPartialBlocksAndCarry) {  // reference values from Python's cryptography
    AesKey key = keyFrom("2b7e151628aed2a6abf7158809cf4f3c");
    AesBlock counter{};
    Bytes iv = unhex("01020304050607080000000000000123");
    std::copy(iv.begin(), iv.end(), counter.begin());
    Bytes data(100);
    for (int i = 0; i < 100; ++i) data[i] = static_cast<std::uint8_t>(i);
    ctrTransform(key, counter, data.data(), data.size());
    EXPECT_EQ(hex(data),
              "3e826115f8f6701d44bc1ec7c5d8b4a329e79213ff7fa87ec7bf9811da3a97979af0dda35c5a58cc5dfcb921908a683668b69bd2"
              "534cae2290eabc7d926d40615c3b26fb1d9bd2d3e32f521bf26e50e0f4b600a59f283cce225279e0690dca83847d4f5a");

    Bytes wrap(32, 0);
    Bytes iv2 = unhex("00000000000000ffffffffffffffffff");
    std::copy(iv2.begin(), iv2.end(), counter.begin());
    ctrTransform(key, counter, wrap.data(), wrap.size());
    EXPECT_EQ(hex(wrap), "dacc9148febbffe342d5805537ea155ff644566de02f529aa57d9a6064ac0ab6");
}

TEST(Aes128Test, NcaCounterFromOffset) {
    AesBlock c = ncaCounter(0x0102030405060708ull, 0x12340);
    EXPECT_EQ(hex(c.data(), c.size()), "01020304050607080000000000001234");
}

TEST(Aes128Test, XtsIeee1619Vector1) {
    AesKey zero{};
    Bytes data(32, 0);
    xtsEncrypt(zero, zero, data.data(), data.size(), 32, 0);
    EXPECT_EQ(hex(data), "917cf69ebd68b2ec9b9fe9a3eadda692cd43d2f59598ed858c02c2652fbf922e");
    xtsDecrypt(zero, zero, data.data(), data.size(), 32, 0);
    EXPECT_EQ(hex(data), std::string(64, '0'));
}

TEST(Aes128Test, XtsNintendoBigEndianSectors) {
    AesKey k1 = keyFrom("000102030405060708090a0b0c0d0e0f");
    AesKey k2 = keyFrom("101112131415161718191a1b1c1d1e1f");
    Bytes data(0x600);
    for (std::size_t i = 0; i < data.size(); ++i) data[i] = static_cast<std::uint8_t>(i * 7 + 3);
    Bytes original = data;
    xtsEncrypt(k1, k2, data.data(), data.size(), 0x200, 5);
    EXPECT_EQ(hex(data.data(), 16), "c25ab09cbee1d8c56df6577b2559ab16");
    EXPECT_EQ(hex(data.data() + 0x400, 16), "1e61c3e79747eca4353cbd3f4b06b761");
    EXPECT_EQ(toHex(Sha256::of(data)), "992b7ecd745a014fdd34f430d8ee2b87bbbb30d585989a470b3067bf4adb9a81");
    xtsDecrypt(k1, k2, data.data(), data.size(), 0x200, 5);
    EXPECT_EQ(data, original);
}
