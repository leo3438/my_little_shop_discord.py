#include <gtest/gtest.h>

#include <cstdlib>
#include <fstream>
#include <iterator>

#include "retromanager/forwarder/IconMaker.hpp"

using namespace rm;
using namespace rm::forwarder;

namespace {

Bytes readFixture(const std::string& relative) {
    std::ifstream in(std::string(RM_FIXTURE_SD_DIR) + "/../ftp_root/" + relative, std::ios::binary);
    return Bytes(std::istreambuf_iterator<char>(in), {});
}

// A w x h picture: a `border`-pixel frame of `edge` around a `centre` fill.
Image framed(int w, int h, int border, Rgb edge, Rgb centre) {
    Image img{w, h, std::vector<std::uint8_t>(static_cast<std::size_t>(w * h * 3))};
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            bool isEdge = x < border || y < border || x >= w - border || y >= h - border;
            img.set(x, y, isEdge ? edge : centre);
        }
    }
    return img;
}

bool near(Rgb a, Rgb b, int tolerance = 12) {
    return std::abs(a.r - b.r) <= tolerance && std::abs(a.g - b.g) <= tolerance && std::abs(a.b - b.b) <= tolerance;
}

constexpr Rgb kGreen{20, 160, 60};
constexpr Rgb kRed{200, 30, 30};

}  // namespace

TEST(IconMakerTest, WideImageIsCentredAndPaddedWithItsEdgeColour) {
    Image wide = framed(400, 200, 6, kGreen, kRed);
    Image square = fitSquare(wide, 256);
    ASSERT_EQ(square.width, 256);
    ASSERT_EQ(square.height, 256);
    EXPECT_TRUE(near(square.at(128, 128), kRed));
    // 400x200 -> 256x128, centred: rows 0..63 and 192..255 are padding.
    EXPECT_TRUE(near(square.at(128, 10), kGreen));
    EXPECT_TRUE(near(square.at(128, 250), kGreen));
    EXPECT_TRUE(near(square.at(0, 0), kGreen));
}

TEST(IconMakerTest, TallImagePadsLeftAndRight) {
    Image tall = framed(90, 300, 4, kGreen, kRed);
    Image square = fitSquare(tall, 256);
    EXPECT_EQ(square.width, 256);
    EXPECT_TRUE(near(square.at(128, 128), kRed));
    EXPECT_TRUE(near(square.at(5, 128), kGreen));
    EXPECT_TRUE(near(square.at(250, 128), kGreen));
}

TEST(IconMakerTest, TinyImageIsUpscaled) {
    Image one = framed(1, 1, 0, kGreen, kRed);
    Image square = fitSquare(one, 256);
    EXPECT_EQ(square.width, 256);
    EXPECT_TRUE(near(square.at(0, 0), kRed));
    EXPECT_TRUE(near(square.at(255, 255), kRed));
}

TEST(IconMakerTest, JpegRoundTrip) {
    Bytes jpeg = encodeJpeg(framed(256, 256, 40, kGreen, kRed), 90);
    ASSERT_GE(jpeg.size(), 3u);
    EXPECT_EQ(jpeg[0], 0xFF);
    EXPECT_EQ(jpeg[1], 0xD8);
    auto decoded = decodeImage(jpeg);
    ASSERT_TRUE(decoded.ok()) << decoded.error().describe();
    EXPECT_EQ(decoded.value().width, 256);
    EXPECT_TRUE(near(decoded.value().at(128, 128), kRed));
    EXPECT_TRUE(near(decoded.value().at(5, 5), kGreen));
}

TEST(IconMakerTest, BoxArtPngBecomesA256JpegUnder128K) {
    Bytes png = readFixture("thumbnails/Nintendo - Super Nintendo Entertainment System/Named_Boxarts/Super Mario World (USA).png");
    ASSERT_FALSE(png.empty());
    auto icon = makeIcon(png);
    ASSERT_TRUE(icon.ok()) << icon.error().describe();
    EXPECT_LE(icon.value().size(), kMaxIconBytes);
    auto decoded = decodeImage(icon.value());
    ASSERT_TRUE(decoded.ok());
    EXPECT_EQ(decoded.value().width, 256);
    EXPECT_EQ(decoded.value().height, 256);
}

TEST(IconMakerTest, ExistingJpegIconIsAccepted) {
    Bytes jpg = readFixture("shop/apps/RetroArch/icon.jpg");
    ASSERT_FALSE(jpg.empty());
    auto icon = makeIcon(jpg);
    ASSERT_TRUE(icon.ok());
    EXPECT_EQ(decodeImage(icon.value()).value().width, 256);
}

TEST(IconMakerTest, NoisyImageStillFitsTheSizeLimit) {
    Image noise{256, 256, std::vector<std::uint8_t>(256 * 256 * 3)};
    std::uint32_t seed = 1;
    for (auto& byte : noise.rgb) {
        seed = seed * 1103515245u + 12345u;
        byte = static_cast<std::uint8_t>(seed >> 16);
    }
    auto icon = makeIcon(encodeJpeg(noise, 100), IconOptions{256, 100, 60 * 1024});
    ASSERT_TRUE(icon.ok()) << icon.error().describe();
    EXPECT_LE(icon.value().size(), 60u * 1024);
}

TEST(IconMakerTest, GarbageIsAParseError) {
    Bytes junk = {'n', 'o', 't', ' ', 'a', 'n', ' ', 'i', 'm', 'a', 'g', 'e'};
    auto icon = makeIcon(junk);
    ASSERT_FALSE(icon.ok());
    EXPECT_EQ(icon.error().code, ErrorCode::ParseError);
    EXPECT_FALSE(makeIcon(Bytes{}).ok());
}

TEST(IconMakerTest, PlaceholderIsAPlain256Jpeg) {
    Bytes icon = placeholderIcon(kRed);
    auto decoded = decodeImage(icon);
    ASSERT_TRUE(decoded.ok());
    EXPECT_EQ(decoded.value().width, 256);
    EXPECT_TRUE(near(decoded.value().at(128, 128), kRed));
    EXPECT_LE(icon.size(), kMaxIconBytes);
}
