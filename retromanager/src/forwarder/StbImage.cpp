// The only translation unit that compiles stb. Everything is static so the
// app can also link Borealis' nanovg, which embeds its own stb_image.
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#include <stb_image.h>

#define STB_IMAGE_RESIZE_STATIC
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#define STBIR_NO_SIMD
#include <stb_image_resize2.h>

#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO
#include <stb_image_write.h>

#include "StbImage.hpp"

namespace rm::forwarder::stb {

bool decodeRgb(const std::uint8_t* data, int size, int& width, int& height, std::vector<std::uint8_t>& rgb) {
    int channels = 0;
    stbi_uc* pixels = stbi_load_from_memory(data, size, &width, &height, &channels, 4);
    if (pixels == nullptr || width <= 0 || height <= 0) {
        if (pixels != nullptr) stbi_image_free(pixels);
        return false;
    }
    std::size_t count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    rgb.resize(count * 3);
    for (std::size_t i = 0; i < count; ++i) {
        unsigned alpha = pixels[i * 4 + 3];
        for (int c = 0; c < 3; ++c) rgb[i * 3 + c] = static_cast<std::uint8_t>(pixels[i * 4 + c] * alpha / 255);
    }
    stbi_image_free(pixels);
    return true;
}

void resizeRgb(const std::uint8_t* in, int inWidth, int inHeight, std::uint8_t* out, int outWidth, int outHeight) {
    stbir_resize_uint8_srgb(in, inWidth, inHeight, inWidth * 3, out, outWidth, outHeight, outWidth * 3, STBIR_RGB);
}

std::vector<std::uint8_t> encodeJpegRgb(const std::uint8_t* rgb, int width, int height, int quality) {
    std::vector<std::uint8_t> out;
    auto sink = [](void* context, void* data, int size) {
        auto* buffer = static_cast<std::vector<std::uint8_t>*>(context);
        auto* bytes = static_cast<std::uint8_t*>(data);
        buffer->insert(buffer->end(), bytes, bytes + size);
    };
    stbi_write_jpg_to_func(sink, &out, width, height, 3, rgb, quality);
    return out;
}

}  // namespace rm::forwarder::stb
