#include "BmpLoader.h"

#include <fstream>
#include <iterator>

namespace {
uint16_t readU16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
uint32_t readU32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}
int32_t readI32(const uint8_t* p) { return static_cast<int32_t>(readU32(p)); }
} // namespace

bool loadBmp24(const std::string& path, BmpImage& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::vector<uint8_t> buf((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (buf.size() < 54 || buf[0] != 'B' || buf[1] != 'M') return false;

    const uint32_t dataOffset = readU32(&buf[10]);
    const uint32_t dibHeaderSize = readU32(&buf[14]);
    if (dibHeaderSize < 40 || buf.size() < 14 + dibHeaderSize) return false;

    const int32_t width = readI32(&buf[18]);
    const int32_t heightRaw = readI32(&buf[22]);
    const uint16_t bitCount = readU16(&buf[28]);
    const uint32_t compression = readU32(&buf[30]);
    if (bitCount != 24 || compression != 0 || width <= 0 || heightRaw == 0) return false;

    const bool bottomUp = heightRaw > 0; // BMP's normal convention; negative height means already top-down
    const int height = bottomUp ? heightRaw : -heightRaw;
    const size_t rowStride = (static_cast<size_t>(width) * 3 + 3) & ~static_cast<size_t>(3); // rows padded to 4 bytes
    if (buf.size() < static_cast<size_t>(dataOffset) + rowStride * static_cast<size_t>(height)) return false;

    out.width = width;
    out.height = height;
    out.rgba.resize(static_cast<size_t>(width) * static_cast<size_t>(height) * 4);
    for (int y = 0; y < height; ++y) {
        const int srcRow = bottomUp ? (height - 1 - y) : y;
        const uint8_t* row = &buf[static_cast<size_t>(dataOffset) + static_cast<size_t>(srcRow) * rowStride];
        uint8_t* dst = &out.rgba[static_cast<size_t>(y) * static_cast<size_t>(width) * 4];
        for (int x = 0; x < width; ++x) {
            // BMP stores pixels as B,G,R (no alpha) for 24-bit images.
            dst[x * 4 + 0] = row[x * 3 + 2]; // R
            dst[x * 4 + 1] = row[x * 3 + 1]; // G
            dst[x * 4 + 2] = row[x * 3 + 0]; // B
            dst[x * 4 + 3] = 255;
        }
    }
    return true;
}
