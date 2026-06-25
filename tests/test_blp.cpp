#include "test.hpp"
#include "blp.hpp"
#include "image.hpp"

#include <cstring>
#include <exception>
#include <vector>

using namespace wf;

namespace {
void put32(std::vector<uint8_t>& b, uint32_t v){ for(int i=0;i<4;i++) b.push_back((v>>(8*i))&0xFF); }
void put16(std::vector<uint8_t>& b, uint16_t v){ b.push_back(v&0xFF); b.push_back((v>>8)&0xFF); }

// Build a minimal BLP2 header (148 bytes) for a width x height image.
std::vector<uint8_t> blpHeader(uint8_t comp, uint8_t aDepth, uint8_t aEnc,
                               uint32_t w, uint32_t h, uint32_t dataOff, uint32_t dataLen) {
    std::vector<uint8_t> b;
    b.insert(b.end(), {'B','L','P','2'});
    put32(b, 1);            // type
    b.push_back(comp);
    b.push_back(aDepth);
    b.push_back(aEnc);
    b.push_back(0);         // hasMips
    put32(b, w);
    put32(b, h);
    put32(b, dataOff);                    // mipOffsets[0]
    for (int i = 1; i < 16; ++i) put32(b, 0);
    put32(b, dataLen);                    // mipSizes[0]
    for (int i = 1; i < 16; ++i) put32(b, 0);
    return b;                             // 0x94 bytes, palette appended by caller if needed
}

uint16_t to565(int r, int g, int b) {
    return static_cast<uint16_t>(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}
} // namespace

void test_blp() {
    std::printf("[blp]\n");

    // --- palettized 2x2, alpha depth 8 ---
    {
        const uint32_t W = 2, H = 2, dataOff = 0x94 + 1024;
        std::vector<uint8_t> b = blpHeader(1, 8, 0, W, H, dataOff, W*H + W*H);
        // palette: 256 BGRA entries; set entry 0=red, 1=green, 2=blue, 3=white.
        std::vector<uint8_t> pal(256 * 4, 0);
        auto setPal = [&](int i, uint8_t r, uint8_t g, uint8_t bl){
            pal[i*4+0]=bl; pal[i*4+1]=g; pal[i*4+2]=r; pal[i*4+3]=255; };
        setPal(0,255,0,0); setPal(1,0,255,0); setPal(2,0,0,255); setPal(3,255,255,255);
        b.insert(b.end(), pal.begin(), pal.end());
        // index data: [0,1,2,3]
        b.push_back(0); b.push_back(1); b.push_back(2); b.push_back(3);
        // alpha data: [255,128,64,0]
        b.push_back(255); b.push_back(128); b.push_back(64); b.push_back(0);

        BlpInfo info;
        Image img = decodeBlp(b, &info);
        CHECK(info.compression == 1);
        CHECK(img.width == 2 && img.height == 2);
        CHECK(img.at(0,0).r == 255 && img.at(0,0).g == 0 && img.at(0,0).b == 0);
        CHECK(img.at(1,0).g == 255);
        CHECK(img.at(0,1).b == 255);
        CHECK(img.at(1,1).r == 255 && img.at(1,1).g == 255 && img.at(1,1).b == 255);
        CHECK(img.at(0,0).a == 255 && img.at(1,0).a == 128);
        CHECK(img.at(0,1).a == 64  && img.at(1,1).a == 0);
    }

    // --- DXT1 single 4x4 block: c0=red, c1=blue, indices select both + interp ---
    {
        const uint32_t W = 4, H = 4, dataOff = 0x94 + 1024;
        std::vector<uint8_t> b = blpHeader(2, 0, 0, W, H, dataOff, 8);
        b.insert(b.end(), 1024, 0); // palette region present but unused
        uint16_t c0 = to565(255,0,0);   // red  (c0 > c1 -> 4-colour mode)
        uint16_t c1 = to565(0,0,255);   // blue
        put16(b, c0); put16(b, c1);
        // index map: row0 all idx0 (red), row1 all idx1 (blue), rows 2-3 idx2/idx3.
        uint32_t bits = 0;
        auto setIdx = [&](int k, uint32_t v){ bits |= (v & 3u) << (k*2); };
        for (int k=0;k<4;k++)  setIdx(k,0);   // pixels 0..3  red
        for (int k=4;k<8;k++)  setIdx(k,1);   // pixels 4..7  blue
        for (int k=8;k<12;k++) setIdx(k,2);   // 2/3 red+blue
        for (int k=12;k<16;k++)setIdx(k,3);
        put32(b, bits);

        Image img = decodeBlp(b);
        CHECK(img.width == 4 && img.height == 4);
        // (0,0) -> red, (0,1) -> blue
        CHECK(img.at(0,0).r > 200 && img.at(0,0).b < 60);
        CHECK(img.at(0,1).b > 200 && img.at(0,1).r < 60);
        // idx2 = (2*c0 + c1)/3 -> reddish purple, more red than blue
        CHECK(img.at(0,2).r > img.at(0,2).b);
        CHECK(img.at(0,0).a == 255);
    }

    // --- DXT5 single block: constant colour, alpha ramp a0=0,a1=255 ---
    {
        const uint32_t W = 4, H = 4, dataOff = 0x94 + 1024;
        std::vector<uint8_t> b = blpHeader(2, 8, 7, W, H, dataOff, 16);
        b.insert(b.end(), 1024, 0);
        // alpha block: a0=0, a1=255 (a0<=a1 -> 6-value mode with 0 and 255 endpoints)
        b.push_back(0); b.push_back(255);
        // 16 * 3-bit alpha indices: pixel0 idx0 (->0), pixel1 idx1 (->255), rest idx0.
        uint64_t abits = 0;
        abits |= (uint64_t)0 << (0*3);
        abits |= (uint64_t)1 << (1*3);
        for (int i = 0; i < 6; ++i) b.push_back((abits >> (i*8)) & 0xFF);
        // colour block: c0=c1=green -> flat green
        uint16_t g = to565(0,255,0);
        put16(b, g); put16(b, g);
        put32(b, 0); // all idx0
        Image img = decodeBlp(b);
        CHECK(img.at(0,0).g > 200);
        CHECK(img.at(0,0).a == 0);     // pixel 0 alpha index 0 -> a0 = 0
        CHECK(img.at(1,0).a == 255);   // pixel 1 alpha index 1 -> a1 = 255
    }

    // --- raw BGRA with two mips: level 0 = 4x2, level 1 = 2x1 ---
    {
        const uint32_t W = 4, H = 2;
        const uint32_t off0 = 0x94 + 1024;          // mip 0 data
        const uint32_t len0 = W * H * 4;            // 32 bytes
        const uint32_t off1 = off0 + len0;          // mip 1 data
        const uint32_t mw1 = W >> 1, mh1 = H >> 1;  // 2 x 1
        const uint32_t len1 = mw1 * mh1 * 4;        // 8 bytes
        // Header carries two populated mips; blpHeader only emits one, so build by hand.
        std::vector<uint8_t> b;
        b.insert(b.end(), {'B','L','P','2'});
        put32(b, 1);              // type
        b.push_back(3);           // compression: raw BGRA
        b.push_back(8);           // alphaDepth
        b.push_back(0);           // alphaEncoding
        b.push_back(1);           // hasMips
        put32(b, W);
        put32(b, H);
        put32(b, off0); put32(b, off1);            // mipOffsets[0..1]
        for (int i = 2; i < 16; ++i) put32(b, 0);
        put32(b, len0); put32(b, len1);            // mipSizes[0..1]
        for (int i = 2; i < 16; ++i) put32(b, 0);
        b.insert(b.end(), 1024, 0);                // palette region (unused by raw)
        // mip 0: 8 red BGRA pixels
        for (uint32_t i = 0; i < W * H; ++i) { b.push_back(0); b.push_back(0); b.push_back(255); b.push_back(255); }
        // mip 1: 2 green BGRA pixels
        for (uint32_t i = 0; i < mw1 * mh1; ++i) { b.push_back(0); b.push_back(255); b.push_back(0); b.push_back(255); }

        BlpInfo info;
        Image m0 = decodeBlpMip(b, 0, &info);
        CHECK(info.mipCount == 2);
        CHECK(m0.width == 4 && m0.height == 2);     // mip 0 full resolution
        CHECK(m0.at(0,0).r == 255 && m0.at(0,0).g == 0);
        Image m1 = decodeBlpMip(b, 1);
        CHECK(m1.width == 2 && m1.height == 1);     // dimensions halved (max(1,...))
        CHECK(m1.at(0,0).g == 255 && m1.at(0,0).r == 0);
        // decodeBlp delegates to mip 0 -> byte-identical to decodeBlpMip(b,0)
        Image base = decodeBlp(b);
        CHECK(base.width == 4 && base.height == 2);
        // out-of-range mip level throws
        bool threw = false;
        try { decodeBlpMip(b, 2); } catch (const std::exception&) { threw = true; }
        CHECK(threw);
    }

    // --- PNG round-trips through our writer (structural sanity) ---
    {
        Image img(2, 2);
        img.at(0,0) = Rgba{255,0,0,255};
        std::vector<uint8_t> png = encodePng(img);
        // PNG signature + IHDR + IDAT + IEND present.
        CHECK(png.size() > 8);
        CHECK(png[0] == 0x89 && png[1] == 'P' && png[2] == 'N' && png[3] == 'G');
        auto findTag = [&](const char* t){
            for (size_t i = 0; i + 4 <= png.size(); ++i)
                if (!std::memcmp(&png[i], t, 4)) return true;
            return false;
        };
        CHECK(findTag("IHDR")); CHECK(findTag("IDAT")); CHECK(findTag("IEND"));
    }
}
