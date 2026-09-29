#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "retromanager/core/Result.hpp"
#include "retromanager/forwarder/Sha256.hpp"  // Bytes

namespace rm::forwarder {

struct Rgb {
    std::uint8_t r = 0, g = 0, b = 0;
};

// 8-bit RGB pixels, row by row, no padding.
struct Image {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgb;

    Rgb at(int x, int y) const;
    void set(int x, int y, Rgb colour);
};

// HOME menu icons are JPEG files of at most 128 KiB (icon_<Language>.dat).
constexpr std::size_t kMaxIconBytes = 0x20000;

struct IconOptions {
    int size = 256;
    int quality = 90;                    // lowered step by step until the file fits
    std::size_t maxBytes = kMaxIconBytes;
};

// PNG, JPEG, BMP, GIF (first frame)... Transparency is flattened onto
// black. ParseError when the data is not a picture.
Result<Image> decodeImage(const Bytes& file);

// The whole picture, scaled to fit a size x size square (aspect ratio kept,
// never cropped: box art must stay readable) and centred; the bands left
// over take the average colour of the picture's border, so they blend in.
Image fitSquare(const Image& image, int size);

Bytes encodeJpeg(const Image& image, int quality);

// decode + fitSquare + encodeJpeg within the size limit.
Result<Bytes> makeIcon(const Bytes& file, const IconOptions& options = {});

// A plain square, for games without box art.
Bytes placeholderIcon(Rgb colour, const IconOptions& options = {});

}  // namespace rm::forwarder
