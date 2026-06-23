#include "image.hpp"
#include <cstdio>

namespace wf {
namespace {

uint32_t crc32(const uint8_t* data, size_t len, uint32_t crc = 0) {
    static uint32_t table[256];
    static bool init = false;
    if (!init) {
        for (uint32_t n = 0; n < 256; ++n) {
            uint32_t c = n;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            table[n] = c;
        }
        init = true;
    }
    crc = crc ^ 0xFFFFFFFFu;
    for (size_t i = 0; i < len; ++i) crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

uint32_t adler32(const uint8_t* data, size_t len) {
    uint32_t a = 1, b = 0;
    const uint32_t MOD = 65521;
    for (size_t i = 0; i < len; ++i) { a = (a + data[i]) % MOD; b = (b + a) % MOD; }
    return (b << 16) | a;
}

void put32be(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back((x >> 24) & 0xFF); v.push_back((x >> 16) & 0xFF);
    v.push_back((x >> 8) & 0xFF);  v.push_back(x & 0xFF);
}

void writeChunk(std::vector<uint8_t>& out, const char tag[4], const std::vector<uint8_t>& data) {
    put32be(out, static_cast<uint32_t>(data.size()));
    std::vector<uint8_t> typeAndData;
    typeAndData.insert(typeAndData.end(), tag, tag + 4);
    typeAndData.insert(typeAndData.end(), data.begin(), data.end());
    out.insert(out.end(), typeAndData.begin(), typeAndData.end());
    put32be(out, crc32(typeAndData.data(), typeAndData.size()));
}

// Wrap raw bytes in a zlib stream using only stored (uncompressed) deflate
// blocks. Valid, just not space-efficient -- fine for tooling output.
std::vector<uint8_t> zlibStore(const std::vector<uint8_t>& raw) {
    std::vector<uint8_t> z;
    z.push_back(0x78); z.push_back(0x01);  // zlib header: CM=8, no preset dict
    size_t pos = 0;
    while (pos < raw.size()) {
        size_t block = raw.size() - pos;
        if (block > 65535) block = 65535;
        bool last = (pos + block >= raw.size());
        z.push_back(last ? 1 : 0);          // BFINAL + BTYPE=00
        uint16_t len = static_cast<uint16_t>(block);
        z.push_back(len & 0xFF); z.push_back((len >> 8) & 0xFF);
        uint16_t nlen = ~len;
        z.push_back(nlen & 0xFF); z.push_back((nlen >> 8) & 0xFF);
        z.insert(z.end(), raw.begin() + pos, raw.begin() + pos + block);
        pos += block;
    }
    put32be(z, adler32(raw.data(), raw.size()));  // Adler is big-endian in zlib
    return z;
}

} // namespace

std::vector<uint8_t> encodePng(const Image& img) {
    std::vector<uint8_t> out = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };

    std::vector<uint8_t> ihdr;
    put32be(ihdr, static_cast<uint32_t>(img.width));
    put32be(ihdr, static_cast<uint32_t>(img.height));
    ihdr.push_back(8);   // bit depth
    ihdr.push_back(6);   // colour type 6 = RGBA
    ihdr.push_back(0);   // compression
    ihdr.push_back(0);   // filter
    ihdr.push_back(0);   // interlace
    writeChunk(out, "IHDR", ihdr);

    // Raw scanlines, each prefixed with filter byte 0 (None).
    std::vector<uint8_t> raw;
    raw.reserve(static_cast<size_t>(img.height) * (1 + img.width * 4));
    for (int y = 0; y < img.height; ++y) {
        raw.push_back(0);
        for (int x = 0; x < img.width; ++x) {
            const Rgba& p = img.at(x, y);
            raw.push_back(p.r); raw.push_back(p.g); raw.push_back(p.b); raw.push_back(p.a);
        }
    }
    writeChunk(out, "IDAT", zlibStore(raw));
    writeChunk(out, "IEND", {});
    return out;
}

bool writePng(const Image& img, const std::string& path) {
    std::vector<uint8_t> bytes = encodePng(img);
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    size_t wrote = std::fwrite(bytes.data(), 1, bytes.size(), f);
    std::fclose(f);
    return wrote == bytes.size();
}

} // namespace wf
