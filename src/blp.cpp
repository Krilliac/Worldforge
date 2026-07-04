#include "blp.hpp"
#include "byte_reader.hpp"
#include <stdexcept>

namespace wf {
namespace {

constexpr size_t kHeaderSize  = 0x94;          // up to (not incl.) palette
constexpr size_t kPaletteSize = 256 * 4;

inline Rgba rgb565(uint16_t c) {
    uint8_t r5 = (c >> 11) & 0x1F, g6 = (c >> 5) & 0x3F, b5 = c & 0x1F;
    Rgba p;
    p.r = static_cast<uint8_t>((r5 << 3) | (r5 >> 2));
    p.g = static_cast<uint8_t>((g6 << 2) | (g6 >> 4));
    p.b = static_cast<uint8_t>((b5 << 3) | (b5 >> 2));
    p.a = 255;
    return p;
}

inline Rgba lerp(const Rgba& a, const Rgba& b, int num, int den) {
    Rgba p;
    p.r = static_cast<uint8_t>((a.r * (den - num) + b.r * num) / den);
    p.g = static_cast<uint8_t>((a.g * (den - num) + b.g * num) / den);
    p.b = static_cast<uint8_t>((a.b * (den - num) + b.b * num) / den);
    p.a = 255;
    return p;
}

// Decode the 8-byte DXT colour block into colours[4] and a 16-entry index map.
// `dxt1Punchthrough` enables the c0<=c1 transparent/black behaviour of DXT1.
void decodeColorBlock(const uint8_t* blk, Rgba colours[4], uint8_t idx[16],
                      bool dxt1Punchthrough, bool& usedPunchAlpha) {
    uint16_t c0 = static_cast<uint16_t>(blk[0] | (blk[1] << 8));
    uint16_t c1 = static_cast<uint16_t>(blk[2] | (blk[3] << 8));
    colours[0] = rgb565(c0);
    colours[1] = rgb565(c1);
    if (c0 > c1 || !dxt1Punchthrough) {
        colours[2] = lerp(colours[0], colours[1], 1, 3);
        colours[3] = lerp(colours[0], colours[1], 2, 3);
        usedPunchAlpha = false;
    } else {
        colours[2] = lerp(colours[0], colours[1], 1, 2);
        colours[3] = Rgba{0, 0, 0, 0};   // transparent black
        usedPunchAlpha = true;
    }
    uint32_t bits = static_cast<uint32_t>(blk[4]) | (blk[5] << 8) |
                    (blk[6] << 16) | (static_cast<uint32_t>(blk[7]) << 24);
    for (int i = 0; i < 16; ++i) idx[i] = (bits >> (i * 2)) & 0x3;
}

void blitBlock(Image& img, int bx, int by, const Rgba colours[4], const uint8_t idx[16],
               const uint8_t* alpha16 /*nullable, 0..255 per pixel*/) {
    for (int py = 0; py < 4; ++py) {
        for (int px = 0; px < 4; ++px) {
            int x = bx + px, y = by + py;
            if (x >= img.width || y >= img.height) continue;
            int k = py * 4 + px;
            Rgba c = colours[idx[k]];
            if (alpha16) c.a = alpha16[k];
            img.at(x, y) = c;
        }
    }
}

void decodeDxt(const std::vector<uint8_t>& buf, size_t off, Image& img,
               uint8_t alphaEncoding) {
    int bw = (img.width + 3) / 4, bh = (img.height + 3) / 4;
    size_t pos = off;
    for (int byi = 0; byi < bh; ++byi) {
        for (int bxi = 0; bxi < bw; ++bxi) {
            Rgba colours[4]; uint8_t idx[16]; uint8_t alpha[16];
            bool punch = false;

            if (alphaEncoding == 0) {           // DXT1
                if (pos + 8 > buf.size()) throw std::runtime_error("DXT1 truncated");
                decodeColorBlock(&buf[pos], colours, idx, /*punchthrough*/true, punch);
                pos += 8;
                blitBlock(img, bxi * 4, byi * 4, colours, idx, nullptr);
            } else if (alphaEncoding == 1) {    // DXT3: 64b explicit 4-bit alpha + colour
                if (pos + 16 > buf.size()) throw std::runtime_error("DXT3 truncated");
                for (int i = 0; i < 8; ++i) {
                    uint8_t byte = buf[pos + i];
                    uint8_t a0 = byte & 0x0F, a1 = (byte >> 4) & 0x0F;
                    alpha[i * 2 + 0] = static_cast<uint8_t>((a0 << 4) | a0);
                    alpha[i * 2 + 1] = static_cast<uint8_t>((a1 << 4) | a1);
                }
                decodeColorBlock(&buf[pos + 8], colours, idx, /*punchthrough*/false, punch);
                pos += 16;
                blitBlock(img, bxi * 4, byi * 4, colours, idx, alpha);
            } else {                            // DXT5 (alphaEncoding 7): interpolated alpha
                if (pos + 16 > buf.size()) throw std::runtime_error("DXT5 truncated");
                uint8_t a0 = buf[pos], a1 = buf[pos + 1];
                uint8_t at[8];
                at[0] = a0; at[1] = a1;
                if (a0 > a1) {
                    for (int i = 1; i <= 6; ++i)
                        at[i + 1] = static_cast<uint8_t>(((7 - i) * a0 + i * a1) / 7);
                } else {
                    for (int i = 1; i <= 4; ++i)
                        at[i + 1] = static_cast<uint8_t>(((5 - i) * a0 + i * a1) / 5);
                    at[6] = 0; at[7] = 255;
                }
                uint64_t abits = 0;
                for (int i = 0; i < 6; ++i)
                    abits |= static_cast<uint64_t>(buf[pos + 2 + i]) << (i * 8);
                for (int i = 0; i < 16; ++i)
                    alpha[i] = at[(abits >> (i * 3)) & 0x7];
                decodeColorBlock(&buf[pos + 8], colours, idx, /*punchthrough*/false, punch);
                pos += 16;
                blitBlock(img, bxi * 4, byi * 4, colours, idx, alpha);
            }
        }
    }
}

void decodePalettized(const std::vector<uint8_t>& buf, const uint8_t* palette,
                      size_t dataOff, Image& img, uint8_t alphaDepth) {
    size_t n = static_cast<size_t>(img.width) * img.height;
    if (dataOff + n > buf.size()) throw std::runtime_error("palettized index data truncated");

    for (size_t i = 0; i < n; ++i) {
        uint8_t pi = buf[dataOff + i];
        const uint8_t* pe = palette + pi * 4;   // BGRA
        Rgba& px = img.pixels[i];
        px.b = pe[0]; px.g = pe[1]; px.r = pe[2]; px.a = 255;
    }
    // Alpha array follows the index array.
    size_t aOff = dataOff + n;
    if (alphaDepth == 8) {
        if (aOff + n > buf.size()) throw std::runtime_error("alpha8 truncated");
        for (size_t i = 0; i < n; ++i) img.pixels[i].a = buf[aOff + i];
    } else if (alphaDepth == 4) {
        size_t bytes = (n + 1) / 2;
        if (aOff + bytes > buf.size()) throw std::runtime_error("alpha4 truncated");
        for (size_t i = 0; i < n; ++i) {
            uint8_t byte = buf[aOff + i / 2];
            uint8_t a4 = (i & 1) ? (byte >> 4) : (byte & 0x0F);
            img.pixels[i].a = static_cast<uint8_t>((a4 << 4) | a4);
        }
    } else if (alphaDepth == 1) {
        size_t bytes = (n + 7) / 8;
        if (aOff + bytes > buf.size()) throw std::runtime_error("alpha1 truncated");
        for (size_t i = 0; i < n; ++i) {
            uint8_t byte = buf[aOff + i / 8];
            img.pixels[i].a = ((byte >> (i & 7)) & 1) ? 255 : 0;
        }
    }
    // alphaDepth 0 -> fully opaque (already 255).
}

} // namespace

namespace {
// Parse the fixed BLP2 header (magic, format fields, dimensions, mip tables).
// Fills `info` (incl. mipCount) and the caller's 16-entry offset/size tables.
// Throws on a non-BLP2, zero-dimension, or JPEG-content (type 0) input.
void parseBlpHeader(const std::vector<uint8_t>& buf, BlpInfo& info,
                    uint32_t (&mipOffsets)[16], uint32_t (&mipSizes)[16]) {
    ByteReader r(buf);
    if (r.fourccRaw() != "BLP2") throw std::runtime_error("not a BLP2 file");

    info = BlpInfo{};
    info.type          = r.u32();
    // type 0 = JPEG-compressed content (BLP0/alpha-era, decoded via Intel's
    // ijl15 in the original client). Vanilla 1.12.1 content is all type 1
    // (direct: palettized / DXT / raw); reject JPEG loudly rather than
    // misread it as a direct blob. See formats/blp-mip-jpeg.md.
    if (info.type == 0)
        throw std::runtime_error("BLP JPEG content (type 0) unsupported; "
                                 "alpha-era format, not used by 1.12.1");
    info.compression   = r.u8();
    info.alphaDepth    = r.u8();
    info.alphaEncoding = r.u8();
    info.hasMips       = r.u8();
    info.width         = r.u32();
    info.height        = r.u32();

    for (int i = 0; i < 16; ++i) mipOffsets[i] = r.u32();
    for (int i = 0; i < 16; ++i) mipSizes[i]   = r.u32();
    for (int i = 0; i < 16; ++i) if (mipOffsets[i] && mipSizes[i]) info.mipCount = i + 1;

    if (info.width == 0 || info.height == 0)
        throw std::runtime_error("BLP has zero dimensions");
}
}  // namespace

BlpInfo readBlpInfo(const std::vector<uint8_t>& buf) {
    BlpInfo info;
    uint32_t mipOffsets[16], mipSizes[16];
    parseBlpHeader(buf, info, mipOffsets, mipSizes);
    return info;
}

int selectBlpMip(const BlpInfo& info, int targetMaxDim) {
    int levels = info.mipCount > 0 ? info.mipCount : 1;
    if (targetMaxDim < 1) targetMaxDim = 1;
    int lvl = 0;
    for (int i = 0; i < levels && i < 16; ++i) {
        uint32_t w = info.width  >> i; if (w == 0) w = 1;
        uint32_t h = info.height >> i; if (h == 0) h = 1;
        uint32_t dim = w > h ? w : h;
        if (static_cast<int>(dim) >= targetMaxDim) lvl = i;  // still covers target
        else break;                                          // smaller than target: stop
    }
    return lvl;
}

Image decodeBlpForSize(const std::vector<uint8_t>& buf, int targetMaxDim, BlpInfo* outInfo) {
    BlpInfo info = readBlpInfo(buf);
    Image img = decodeBlpMip(buf, selectBlpMip(info, targetMaxDim), outInfo);
    if (outInfo) *outInfo = info;   // report the whole-texture info, not just the mip
    return img;
}

Image decodeBlpMip(const std::vector<uint8_t>& buf, int mipLevel, BlpInfo* outInfo) {
    BlpInfo info;
    uint32_t mipOffsets[16], mipSizes[16];
    parseBlpHeader(buf, info, mipOffsets, mipSizes);

    if (mipLevel < 0 || mipLevel >= 16 || mipLevel >= info.mipCount)
        throw std::runtime_error("BLP mip level out of range");

    // Mip 0 falls back to the canonical post-palette offset; deeper mips must
    // carry an explicit, in-range offset/size in the tables.
    size_t dataOff = mipOffsets[mipLevel]
                         ? mipOffsets[mipLevel]
                         : (mipLevel == 0 ? (kHeaderSize + kPaletteSize) : 0);
    if (dataOff == 0 || mipSizes[mipLevel] == 0 || dataOff > buf.size())
        throw std::runtime_error("BLP mip offset/size out of range");

    uint32_t mw = info.width  >> mipLevel; if (mw == 0) mw = 1;
    uint32_t mh = info.height >> mipLevel; if (mh == 0) mh = 1;
    Image img(static_cast<int>(mw), static_cast<int>(mh));

    if (info.compression == 1) {
        if (buf.size() < kHeaderSize + kPaletteSize)
            throw std::runtime_error("palettized BLP missing palette");
        decodePalettized(buf, buf.data() + kHeaderSize, dataOff, img, info.alphaDepth);
    } else if (info.compression == 2) {
        decodeDxt(buf, dataOff, img, info.alphaEncoding);
    } else if (info.compression == 3) {
        size_t n = static_cast<size_t>(img.width) * img.height;
        if (dataOff + n * 4 > buf.size()) throw std::runtime_error("raw BGRA truncated");
        for (size_t i = 0; i < n; ++i) {
            const uint8_t* p = buf.data() + dataOff + i * 4;  // BGRA
            img.pixels[i] = Rgba{ p[2], p[1], p[0], p[3] };
        }
    } else {
        throw std::runtime_error("unsupported BLP compression " +
                                 std::to_string(info.compression));
    }

    if (outInfo) *outInfo = info;
    return img;
}

Image decodeBlp(const std::vector<uint8_t>& buf, BlpInfo* outInfo) {
    return decodeBlpMip(buf, 0, outInfo);
}

} // namespace wf
