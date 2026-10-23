// Derecho Engine - image import and export without external dependencies.
//
// PNG decoding needs an inflate implementation; rather than pulling in zlib or
// stb, this file carries a small, straightforward one (fixed and dynamic
// Huffman, stored blocks) plus a PNG writer that emits uncompressed deflate
// blocks. TGA (uncompressed and RLE) covers the other common case. Everything
// ends up as 8-bit RGBA so the renderer only ever sees one layout.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace derecho::asset {

struct Image {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> pixels; // RGBA8, row-major, top row first

    bool Empty() const { return width <= 0 || height <= 0 || pixels.empty(); }
    const uint8_t* At(int x, int y) const {
        return pixels.data() + (static_cast<size_t>(y) * static_cast<size_t>(width) +
                                static_cast<size_t>(x)) * 4u;
    }
    uint8_t* At(int x, int y) {
        return pixels.data() + (static_cast<size_t>(y) * static_cast<size_t>(width) +
                                static_cast<size_t>(x)) * 4u;
    }
};

enum class ImageFormat { Unknown, Png, Tga };

ImageFormat ImageFormatFromPath(const std::string& path);
const char* ImageFormatName(ImageFormat format);

// Dispatches on the file extension; `error` explains the failure.
// (Named *File* because <windows.h> defines LoadImage as a macro, which any
// translation unit that includes SDL.h would otherwise pick up.)
bool LoadImageFile(const std::string& path, Image& out, std::string& error);

bool DecodePng(const uint8_t* data, size_t size, Image& out, std::string& error);
bool DecodeTga(const uint8_t* data, size_t size, Image& out, std::string& error);

bool SavePng(const Image& image, const std::string& path, std::string& error);
bool SaveTga(const Image& image, const std::string& path, std::string& error);

// Raw deflate/zlib helpers, exposed so the test suite can exercise them.
bool ZlibInflate(const uint8_t* data, size_t size, std::vector<uint8_t>& out, std::string& error);
void ZlibStore(const std::vector<uint8_t>& input, std::vector<uint8_t>& out);

} // namespace derecho::asset
