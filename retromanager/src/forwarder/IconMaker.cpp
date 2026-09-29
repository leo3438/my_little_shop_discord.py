#include "retromanager/forwarder/IconMaker.hpp"

#include <algorithm>
#include <limits>

#include "StbImage.hpp"

namespace rm::forwarder {

Rgb Image::at(int x, int y) const {
    std::size_t i = (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)) * 3;
    return Rgb{rgb[i], rgb[i + 1], rgb[i + 2]};
}

void Image::set(int x, int y, Rgb colour) {
    std::size_t i = (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)) * 3;
    rgb[i] = colour.r;
    rgb[i + 1] = colour.g;
    rgb[i + 2] = colour.b;
}

Result<Image> decodeImage(const Bytes& file) {
    if (file.empty() || file.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return Error{ErrorCode::ParseError, "empty or oversized picture"};
    }
    Image image;
    if (!stb::decodeRgb(file.data(), static_cast<int>(file.size()), image.width, image.height, image.rgb)) {
        return Error{ErrorCode::ParseError, "unsupported or corrupt picture"};
    }
    return image;
}

namespace {

Rgb borderAverage(const Image& image) {
    std::uint64_t sum[3] = {0, 0, 0};
    std::uint64_t count = 0;
    auto add = [&](int x, int y) {
        Rgb p = image.at(x, y);
        sum[0] += p.r;
        sum[1] += p.g;
        sum[2] += p.b;
        ++count;
    };
    for (int x = 0; x < image.width; ++x) {
        add(x, 0);
        if (image.height > 1) add(x, image.height - 1);
    }
    for (int y = 1; y + 1 < image.height; ++y) {
        add(0, y);
        if (image.width > 1) add(image.width - 1, y);
    }
    return Rgb{static_cast<std::uint8_t>(sum[0] / count), static_cast<std::uint8_t>(sum[1] / count),
               static_cast<std::uint8_t>(sum[2] / count)};
}

}  // namespace

Image fitSquare(const Image& image, int size) {
    Image out{size, size, std::vector<std::uint8_t>(static_cast<std::size_t>(size) * static_cast<std::size_t>(size) * 3)};
    if (image.width <= 0 || image.height <= 0) return out;

    int w = size, h = size;
    if (image.width > image.height) {
        h = std::max(1, static_cast<int>(static_cast<long long>(image.height) * size / image.width));
    } else if (image.height > image.width) {
        w = std::max(1, static_cast<int>(static_cast<long long>(image.width) * size / image.height));
    }
    Image scaled{w, h, std::vector<std::uint8_t>(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 3)};
    stb::resizeRgb(image.rgb.data(), image.width, image.height, scaled.rgb.data(), w, h);

    Rgb fill = borderAverage(scaled);
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) out.set(x, y, fill);
    }
    int left = (size - w) / 2, top = (size - h) / 2;
    for (int y = 0; y < h; ++y) {
        std::copy_n(scaled.rgb.begin() + static_cast<std::ptrdiff_t>(y) * w * 3, w * 3,
                    out.rgb.begin() + (static_cast<std::ptrdiff_t>(top + y) * size + left) * 3);
    }
    return out;
}

Bytes encodeJpeg(const Image& image, int quality) {
    return stb::encodeJpegRgb(image.rgb.data(), image.width, image.height, std::clamp(quality, 1, 100));
}

namespace {

Result<Bytes> encodeWithin(const Image& square, const IconOptions& options) {
    for (int quality = options.quality; quality >= 10; quality -= 10) {
        Bytes jpeg = encodeJpeg(square, quality);
        if (!jpeg.empty() && jpeg.size() <= options.maxBytes) return jpeg;
    }
    return Error{ErrorCode::InvalidArgument, "the icon does not fit in " + std::to_string(options.maxBytes) + " bytes"};
}

}  // namespace

Result<Bytes> makeIcon(const Bytes& file, const IconOptions& options) {
    auto image = decodeImage(file);
    if (!image) return image.error();
    return encodeWithin(fitSquare(image.value(), options.size), options);
}

Bytes placeholderIcon(Rgb colour, const IconOptions& options) {
    Image plain{1, 1, {colour.r, colour.g, colour.b}};
    return encodeWithin(fitSquare(plain, options.size), options).valueOr(Bytes{});
}

}  // namespace rm::forwarder
