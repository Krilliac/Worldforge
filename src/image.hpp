#pragma once
// ---------------------------------------------------------------------------
// Image: a simple 8-bit RGBA buffer plus a dependency-free PNG writer (stored
// zlib blocks, real CRC32 + Adler32). Lets the engine emit textures and
// rendered frames without linking libpng/zlib. Not optimised; correctness over
// speed (the writer is for tooling/screenshots, not a hot path).
// ---------------------------------------------------------------------------
#include <cstdint>
#include <string>
#include <vector>

namespace wf {

struct Rgba { uint8_t r = 0, g = 0, b = 0, a = 255; };

struct Image {
    int width = 0, height = 0;
    std::vector<Rgba> pixels;   // row-major, top-left origin

    Image() = default;
    Image(int w, int h) : width(w), height(h), pixels(static_cast<size_t>(w) * h) {}

    Rgba&       at(int x, int y)       { return pixels[static_cast<size_t>(y) * width + x]; }
    const Rgba& at(int x, int y) const { return pixels[static_cast<size_t>(y) * width + x]; }
};

// Encode `img` as PNG bytes (8-bit RGBA, no interlacing).
std::vector<uint8_t> encodePng(const Image& img);

// Write `img` to `path` as PNG. Returns false on file error.
bool writePng(const Image& img, const std::string& path);

} // namespace wf
