#pragma once

// Thin wrappers over stb, private to src/forwarder: the stb functions are
// compiled static in StbImage.cpp only.

#include <cstdint>
#include <vector>

namespace rm::forwarder::stb {

// RGBA pixels flattened onto black, or false.
bool decodeRgb(const std::uint8_t* data, int size, int& width, int& height, std::vector<std::uint8_t>& rgb);

void resizeRgb(const std::uint8_t* in, int inWidth, int inHeight, std::uint8_t* out, int outWidth, int outHeight);

std::vector<std::uint8_t> encodeJpegRgb(const std::uint8_t* rgb, int width, int height, int quality);

}  // namespace rm::forwarder::stb
