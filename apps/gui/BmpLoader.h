#pragma once
#include <cstdint>
#include <string>
#include <vector>

// Minimal loader for uncompressed 24-bit BITMAPINFOHEADER-format .bmp files
// - the only variant the ALG connection-diagram assets under assets/ use
// (confirmed via `file` on all 8 shipped opn_al*.bmp). NOT a general-purpose
// BMP decoder: no RLE/paletted/16-bit/32-bit/BITMAPV4+ header support.
struct BmpImage {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> rgba; // width*height*4 bytes, top-to-bottom row order
                               // (matches ImGui::Image()'s default UV convention),
                               // alpha always 255.
};

bool loadBmp24(const std::string& path, BmpImage& out);
