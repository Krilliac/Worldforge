#include "test.hpp"
#include "asset_loader.hpp"
#include "mpq.hpp"
#include "raster.hpp"
#include "image.hpp"
#include "math.hpp"

#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

using namespace wf;

namespace {
void p8 (std::vector<uint8_t>& b, uint8_t v){ b.push_back(v); }
void p16(std::vector<uint8_t>& b, uint16_t v){ b.push_back(v&0xFF); b.push_back((v>>8)&0xFF); }
void p32(std::vector<uint8_t>& b, uint32_t v){ for(int i=0;i<4;i++) b.push_back((v>>(8*i))&0xFF); }
void pf (std::vector<uint8_t>& b, float f){ uint32_t v; std::memcpy(&v,&f,4); p32(b,v); }
void praw(std::vector<uint8_t>& b, const char* s){ for(int i=0;i<4;i++) b.push_back((uint8_t)s[i]); }
void chunk(std::vector<uint8_t>& b, const char* m, const std::vector<uint8_t>& p){
    // ADT chunk magics are stored reversed on disk; forEachChunk un-reverses.
    b.push_back(m[3]); b.push_back(m[2]); b.push_back(m[1]); b.push_back(m[0]);
    p32(b, (uint32_t)p.size()); b.insert(b.end(), p.begin(), p.end());
}

// A minimal raw (BGRA8888) BLP2 with a single mip.
std::vector<uint8_t> makeRawBlp(int w, int h, Rgba color) {
    std::vector<uint8_t> b;
    praw(b, "BLP2"); p32(b, 1);            // type
    p8(b,3); p8(b,8); p8(b,0); p8(b,0);    // compression=raw, alphaDepth, enc, mips
    p32(b, (uint32_t)w); p32(b, (uint32_t)h);
    for (int i=0;i<16;i++) p32(b, i==0 ? 0x94u : 0u);   // mipOffsets
    for (int i=0;i<16;i++) p32(b, i==0 ? (uint32_t)(w*h*4) : 0u); // mipSizes
    for (int i=0;i<w*h;i++) { p8(b,color.b); p8(b,color.g); p8(b,color.r); p8(b,color.a); }
    return b;
}

// One base-layer MCNK referencing MTEX texture 0.
std::vector<uint8_t> makeMcnk() {
    std::vector<uint8_t> hdr(128, 0);
    auto w32 = [&](size_t off, uint32_t v){ for(int i=0;i<4;i++) hdr[off+i]=(v>>(8*i))&0xFF; };
    w32(0x0C, 1);                          // nLayers = 1
    std::vector<uint8_t> mcvt; for (int i=0;i<145;i++) pf(mcvt, 0.0f);
    std::vector<uint8_t> mcnr;
    for (int i=0;i<145;i++){ mcnr.push_back(0); mcnr.push_back(127); mcnr.push_back(0); }
    for (int i=0;i<13;i++) mcnr.push_back(0);
    std::vector<uint8_t> mcly;
    p32(mcly, 0);  p32(mcly, 0); p32(mcly, 0); p32(mcly, 0);   // textureId 0, flags 0, ofs 0, effect 0
    std::vector<uint8_t> body = hdr;
    chunk(body, "MCVT", mcvt);
    chunk(body, "MCNR", mcnr);
    chunk(body, "MCLY", mcly);
    std::vector<uint8_t> out; chunk(out, "MCNK", body); return out;
}

std::vector<uint8_t> makeAdt() {
    std::vector<uint8_t> mtex; for (char c : std::string("test.blp")) mtex.push_back((uint8_t)c); mtex.push_back(0);
    std::vector<uint8_t> adt;
    chunk(adt, "MTEX", mtex);
    std::vector<uint8_t> mcnk = makeMcnk();
    adt.insert(adt.end(), mcnk.begin(), mcnk.end());
    return adt;
}

std::vector<uint8_t> makeWdt() {
    std::vector<uint8_t> mphd; p32(mphd, 0);            // flags = 0 (4-bit alpha)
    std::vector<uint8_t> main(64*64*8, 0);
    size_t e = (size_t)(32*64 + 32) * 8;                // tile (x=32,y=32)
    main[e] = 1;                                        // flags & 1 -> has ADT
    std::vector<uint8_t> wdt;
    chunk(wdt, "MPHD", mphd);
    chunk(wdt, "MAIN", main);
    return wdt;
}
} // namespace

void test_asset() {
    std::printf("[asset]\n");

    const char* mpqPath = "wforge_asset_test.mpq";
    std::vector<std::pair<std::string, std::vector<uint8_t>>> files = {
        { "test.blp", makeRawBlp(2, 2, Rgba{40, 200, 60, 255}) },
        { "World\\Maps\\TestMap\\TestMap.wdt", makeWdt() },
        { "World\\Maps\\TestMap\\TestMap_32_32.adt", makeAdt() },
    };
    CHECK(writeMpqArchive(mpqPath, files));

    MpqManager mgr;
    CHECK(mgr.addArchive(mpqPath));
    AssetLoader loader(mgr);

    // --- texture decode + cache + fallback ----------------------------------
    auto t = loader.texture("test.blp");
    CHECK(t && t->width == 2 && t->height == 2);
    CHECK(t->at(0,0).g > 150 && t->at(0,0).r < 100);     // the green we authored
    CHECK(loader.texture("test.blp").get() == t.get());  // cached (same object)
    auto miss = loader.texture("nope.blp");
    CHECK(miss && miss->width > 0);                      // fallback, no crash

    // --- WDT: the tile we flagged exists ------------------------------------
    Wdt wdt;
    CHECK(loader.loadWdt("TestMap", wdt));
    CHECK(wdt.tiles[32*64 + 32]);
    CHECK(!wdt.tiles[0]);

    // --- ADT: chunks + MTEX names -------------------------------------------
    Adt adt; std::vector<MapChunk> chunks;
    CHECK(loader.loadAdt("TestMap", 32, 32, adt, chunks));
    CHECK(chunks.size() == 1);
    CHECK(adt.textures.size() == 1 && adt.textures[0] == "test.blp");
    CHECK(chunks[0].layers.size() == 1 && chunks[0].layers[0].textureId == 0);

    // --- buildTile: textured terrain wired to the decoded BLP ---------------
    TileRender tile = loader.buildTile("TestMap", 32, 32);
    CHECK(!tile.empty());
    CHECK(tile.chunkMeshes.size() == 1);
    CHECK(tile.chunkLayers.size() == 1 && tile.chunkLayers[0].size() == 1);
    CHECK(tile.chunkLayers[0][0].texture == t.get());    // layer 0 -> our green BLP

    // It renders the green terrain.
    Framebuffer fb(64, 64); fb.clear(Rgba{0,0,0,255});
    Mat4 view = Mat4::lookAt({-20,-20,40}, {-16,-16,0}, {0,0,1});
    Mat4 proj = Mat4::perspective(60.0, 1.0, 1.0, 500.0);
    tile.renderTerrain(fb, proj * view, {0,0,1});
    bool green = false;
    for (const Rgba& p : fb.color.pixels) if (p.g > 80 && p.g > p.r && p.g > p.b) { green = true; break; }
    CHECK(green);

    // --- a missing tile loads as empty, not a crash -------------------------
    TileRender none = loader.buildTile("TestMap", 5, 5);
    CHECK(none.empty());

    std::remove(mpqPath);
}
