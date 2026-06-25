#include "test.hpp"
#include "terrain.hpp"
#include "coords.hpp"

#include <cstring>
#include <vector>

using namespace wf;

namespace {
void put32(std::vector<uint8_t>& b, uint32_t v){ for(int i=0;i<4;i++) b.push_back((v>>(8*i))&0xFF); }
void put16(std::vector<uint8_t>& b, uint16_t v){ b.push_back(v&0xFF); b.push_back((v>>8)&0xFF); }
void putf (std::vector<uint8_t>& b, float f){ uint32_t v; std::memcpy(&v,&f,4); put32(b,v); }
void magic(std::vector<uint8_t>& b, const char* m){ b.push_back(m[3]); b.push_back(m[2]); b.push_back(m[1]); b.push_back(m[0]); }
void chunk(std::vector<uint8_t>& b, const char* m, const std::vector<uint8_t>& p){
    magic(b,m); put32(b,(uint32_t)p.size()); b.insert(b.end(),p.begin(),p.end());
}

// Build one MCNK chunk with a known header, MCVT, MCNR and one MCLY layer.
std::vector<uint8_t> makeMCNK(uint16_t holes, float baseZ, uint32_t idxX, uint32_t idxY) {
    std::vector<uint8_t> hdr(128, 0);
    auto w32 = [&](size_t off, uint32_t v){ for(int i=0;i<4;i++) hdr[off+i]=(v>>(8*i))&0xFF; };
    auto w16 = [&](size_t off, uint16_t v){ hdr[off]=v&0xFF; hdr[off+1]=(v>>8)&0xFF; };
    auto wf_ = [&](size_t off, float f){ uint32_t v; std::memcpy(&v,&f,4); w32(off,v); };
    w32(0x04, idxX);     // IndexX
    w32(0x08, idxY);     // IndexY
    w32(0x0C, 1);        // nLayers
    w32(0x34, 1234);     // areaId
    w16(0x3C, holes);    // holes
    wf_(0x68, 111.0f);   // position.x
    wf_(0x6C, 222.0f);   // position.y
    wf_(0x70, baseZ);    // position.z (height base)

    // MCVT: 145 heights, value == index so we can verify wiring exactly.
    std::vector<uint8_t> mcvt;
    for (int i = 0; i < 145; ++i) putf(mcvt, (float)i);

    // MCNR: 145 * int8[3] in file order (x,y,z) all pointing straight up
    // (0,0,127) + 13 pad. Matches real vanilla tiles where flat ground stores
    // its up component in the LAST byte.
    std::vector<uint8_t> mcnr;
    for (int i = 0; i < 145; ++i) { mcnr.push_back(0); mcnr.push_back(0); mcnr.push_back(127); }
    for (int i = 0; i < 13; ++i) mcnr.push_back(0);

    // MCLY: one layer.
    std::vector<uint8_t> mcly;
    put32(mcly, 7);     // textureId
    put32(mcly, 0x100); // flags: use_alpha_map
    put32(mcly, 0);     // ofsAlpha
    put32(mcly, 42);    // effectId

    std::vector<uint8_t> body = hdr;
    chunk(body, "MCVT", mcvt);
    chunk(body, "MCNR", mcnr);
    chunk(body, "MCLY", mcly);

    std::vector<uint8_t> out;
    chunk(out, "MCNK", body);
    return out;
}
} // namespace

void test_terrain() {
    std::printf("[terrain]\n");

    // --- parse a single-chunk ADT ---
    std::vector<uint8_t> adt = makeMCNK(/*holes*/0, /*baseZ*/1000.0f, /*idxX*/3, /*idxY*/5);
    auto chunks = parseChunks(adt);
    CHECK(chunks.size() == 1);
    const MapChunk& mc = chunks[0];
    CHECK(mc.indexX == 3 && mc.indexY == 5);
    CHECK(mc.areaId == 1234);
    CHECK_APPROX(mc.position.z, 1000.0f);
    CHECK(mc.layers.size() == 1);
    CHECK(mc.layers[0].textureId == 7 && mc.layers[0].effectId == 42);
    CHECK_APPROX(mc.heights[0],   0.0f);
    CHECK_APPROX(mc.heights[144], 144.0f);
    // Normal bytes (0,0,127) in file order (x,y,z) -> world up (+Z).
    CHECK_APPROX(mc.normals[0].z, 1.0f);
    CHECK_APPROX(mc.normals[0].x, 0.0f);
    CHECK_APPROX(mc.normals[0].y, 0.0f);

    // --- mesh: full chunk, no holes ---
    Mesh full = buildChunkMesh(mc, /*blockX*/30, /*blockY*/30);
    CHECK(full.vertices.size() == 145);
    CHECK(full.indices.size() == 8u * 8u * 4u * 3u);   // 768

    // Outer vertex (0,0) sits at the chunk NW corner with absolute height
    // baseZ + MCVT[0]. Indices into the vertex buffer: outer (i,j) = i*9+j.
    {
        Vec3 corner = chunkCornerWorld(30, 30, /*row=idxY*/5, /*col=idxX*/3, 1000.0f);
        const Vertex& v00 = full.vertices[0];
        CHECK_APPROX(v00.position.x, corner.x);
        CHECK_APPROX(v00.position.y, corner.y);
        CHECK_APPROX(v00.position.z, 1000.0f + 0.0f);
    }
    // Inner vertex (0,0) = buffer index 81, MCVT index 9, height baseZ+9.
    CHECK_APPROX(full.vertices[81].position.z, 1000.0f + 9.0f);

    // Every index must be in range and no triangle degenerate (all distinct).
    bool allInRange = true, noDegenerate = true;
    for (size_t i = 0; i < full.indices.size(); i += 3) {
        uint32_t a = full.indices[i], b = full.indices[i+1], c = full.indices[i+2];
        if (a >= 145 || b >= 145 || c >= 145) allInRange = false;
        if (a == b || b == c || a == c)       noDegenerate = false;
    }
    CHECK(allInRange);
    CHECK(noDegenerate);

    // --- holes remove triangles ---
    // Set hole bit 0 (covers cells rows0-1, cols0-1 => four 4-triangle cells).
    std::vector<uint8_t> adtH = makeMCNK(/*holes*/0x1, 0.0f, 0, 0);
    MapChunk mch = parseChunks(adtH)[0];
    CHECK(cellIsHole(0x1, 0, 0));
    CHECK(cellIsHole(0x1, 1, 1));
    CHECK(!cellIsHole(0x1, 2, 2));
    Mesh holed = buildChunkMesh(mch, 0, 0);
    // 4 cells holed * 4 triangles * 3 indices removed.
    CHECK(holed.indices.size() == full.indices.size() - 4u * 4u * 3u);

    // --- MCAL alpha-map decode ---------------------------------------------
    // Layer 0 is the opaque base: always 255, even with an empty blob.
    {
        MapChunk a;
        a.layers.resize(1);            // just the base layer
        AlphaMap base = decodeAlphaMap(a, 0, /*bigAlpha*/false);
        CHECK(base.at(0, 0) == 255);
        CHECK(base.at(63, 63) == 255);
    }

    // Vanilla packed 4-bit: 0xF nibbles -> 255, 0x0 -> 0. Pack a column ramp so
    // we can verify the low/high-nibble ordering and the *17 scaling.
    {
        MapChunk a;
        a.layers.resize(2);
        a.layers[0].flags = 0;                         // base
        a.layers[1].flags = MCLY_USE_ALPHA;            // 4-bit uncompressed
        a.layers[1].ofsAlpha = 0;
        // 2048 bytes; set every byte to 0xF0 -> even texel nibble 0x0,
        // odd texel nibble 0xF.  texel(0)=0, texel(1)=255, ...
        a.alpha.assign(2048, 0xF0);
        AlphaMap m = decodeAlphaMap(a, 1, /*bigAlpha*/false);
        CHECK(m.at(0, 0) == 0);            // texel 0 -> low nibble 0x0
        CHECK(m.at(0, 1) == 255);          // texel 1 -> high nibble 0xF
        CHECK(m.at(63, 63) == 255);        // texel 4095 is odd -> high nibble

        // All-0xFF blob -> fully opaque everywhere.
        a.alpha.assign(2048, 0xFF);
        AlphaMap full255 = decodeAlphaMap(a, 1, false);
        CHECK(full255.at(10, 10) == 255);
        CHECK(full255.at(63, 0) == 255);
    }

    // Big-alpha 8-bit: one byte per texel, straight through. Use a second layer
    // at a non-zero MCAL offset to exercise ofsAlpha.
    {
        MapChunk a;
        a.layers.resize(2);
        a.layers[0].flags = 0;
        a.layers[1].flags = MCLY_USE_ALPHA;
        a.layers[1].ofsAlpha = 4096;                   // map starts after a gap
        a.alpha.assign(4096, 0x11);                    // padding before the map
        for (int k = 0; k < 64 * 64; ++k)
            a.alpha.push_back(static_cast<uint8_t>(k & 0xFF));
        AlphaMap m = decodeAlphaMap(a, 1, /*bigAlpha*/true);
        CHECK(m.at(0, 0) == 0);
        CHECK(m.at(0, 1) == 1);
        CHECK(m.at(1, 0) == (64 & 0xFF));              // texel 64
        CHECK(m.at(63, 63) == (4095 & 0xFF));          // 4095 & 0xFF == 255
    }

    // Compressed (RLE): a copy run of 4 explicit bytes, then a fill for the rest.
    {
        MapChunk a;
        a.layers.resize(2);
        a.layers[0].flags = 0;
        a.layers[1].flags = MCLY_USE_ALPHA | MCLY_COMPRESSED;
        a.layers[1].ofsAlpha = 0;
        std::vector<uint8_t>& blob = a.alpha;
        blob.push_back(0x04);                          // copy 4
        blob.push_back(10); blob.push_back(20);
        blob.push_back(30); blob.push_back(40);
        int remaining = 64 * 64 - 4;                   // fill the rest with 200
        while (remaining > 0) {
            int run = remaining > 127 ? 127 : remaining;
            blob.push_back(static_cast<uint8_t>(0x80 | run));  // fill `run`
            blob.push_back(200);
            remaining -= run;
        }
        AlphaMap m = decodeAlphaMap(a, 1, /*bigAlpha*/false);
        CHECK(m.at(0, 0) == 10);
        CHECK(m.at(0, 1) == 20);
        CHECK(m.at(0, 2) == 30);
        CHECK(m.at(0, 3) == 40);
        CHECK(m.at(0, 4) == 200);                      // first filled texel
        CHECK(m.at(63, 63) == 200);                    // last texel filled

        // Truncated compressed stream decodes the prefix and leaves the rest
        // transparent instead of reading past the blob.
        a.alpha = { 0x02, 99, 88 };                    // copy 2 then EOF
        AlphaMap t = decodeAlphaMap(a, 1, false);
        CHECK(t.at(0, 0) == 99);
        CHECK(t.at(0, 1) == 88);
        CHECK(t.at(0, 2) == 0);
        CHECK(t.at(63, 63) == 0);
    }

    // A layer without the use-alpha flag, or an out-of-range index, is
    // transparent rather than reading garbage.
    {
        MapChunk a;
        a.layers.resize(2);
        a.layers[1].flags = 0;                         // no MCLY_USE_ALPHA
        a.alpha.assign(2048, 0xFF);
        CHECK(decodeAlphaMap(a, 1, false).at(0, 0) == 0);
        CHECK(decodeAlphaMap(a, 5, false).at(0, 0) == 0);   // no layer 5
    }

    // --- MCAL encode (write path) + round-trip ------------------------------
    {
        // 8-bit big-alpha is loss-less: encode then decode reproduces exactly.
        AlphaMap src;
        for (int k = 0; k < 64 * 64; ++k) src.texels[k] = static_cast<uint8_t>(k & 0xFF);
        std::vector<uint8_t> enc8 = encodeAlphaMap(src, /*bigAlpha*/true);
        CHECK(enc8.size() == 4096);
        MapChunk a;
        a.layers.resize(2);
        a.layers[1].flags = MCLY_USE_ALPHA;
        a.layers[1].ofsAlpha = 0;
        a.alpha = enc8;
        AlphaMap back8 = decodeAlphaMap(a, 1, /*bigAlpha*/true);
        bool exact8 = true;
        for (int k = 0; k < 64 * 64; ++k) if (back8.texels[k] != src.texels[k]) exact8 = false;
        CHECK(exact8);

        // 4-bit packs two texels/byte: 2048 bytes, multiples of 17 round-trip.
        AlphaMap q;
        for (int k = 0; k < 64 * 64; ++k) q.texels[k] = static_cast<uint8_t>((k % 16) * 17);
        std::vector<uint8_t> enc4 = encodeAlphaMap(q, /*bigAlpha*/false);
        CHECK(enc4.size() == 2048);
        a.alpha = enc4;
        AlphaMap back4 = decodeAlphaMap(a, 1, /*bigAlpha*/false);
        bool exact4 = true;
        for (int k = 0; k < 64 * 64; ++k) if (back4.texels[k] != q.texels[k]) exact4 = false;
        CHECK(exact4);
        // 255 quantises to nibble 15 -> 255 (not 0): the rounding boundary.
        AlphaMap full; full.texels.fill(255);
        a.alpha = encodeAlphaMap(full, false);
        CHECK(decodeAlphaMap(a, 1, false).at(0, 0) == 255);
    }

    // --- fixAlphaMapEdges: 63->64 draw-time edge duplication ----------------
    {
        // A map whose value == column index makes the column fix observable, and
        // whose row 63 differs from row 62 makes the row fix observable too.
        AlphaMap m;
        for (int r = 0; r < 64; ++r)
            for (int c = 0; c < 64; ++c)
                m.texels[r * 64 + c] = static_cast<uint8_t>(r == 63 ? 7 : c);
        fixAlphaMapEdges(m);
        // Last column copied from the second-to-last (col 63 := col 62 == 62).
        for (int r = 0; r < 62; ++r) CHECK(m.at(r, 63) == 62);
        // Last row copied from the second-to-last (row 62: value == column).
        for (int c = 0; c < 62; ++c) CHECK(m.at(63, c) == c);
        // Corner inherits texel (62,62) via the column-then-row order.
        CHECK(m.at(63, 63) == 62);
        // Interior is untouched.
        CHECK(m.at(10, 20) == 20);

        // decodeAlphaMap itself stays a loss-less codec (no implicit fix): a
        // chunk without DO_NOT_FIX still decodes raw edges; the fix is applied
        // only by the renderer (asset_loader), proven by the round-trip above.
        MapChunk c2; c2.layers.resize(2); c2.layers[1].flags = MCLY_USE_ALPHA;
        AlphaMap edged; for (int k = 0; k < 64*64; ++k) edged.texels[k] = static_cast<uint8_t>(k & 0xFF);
        c2.alpha = encodeAlphaMap(edged, true);
        CHECK(!(c2.flags & MCNK_DO_NOT_FIX_ALPHA));
        AlphaMap raw = decodeAlphaMap(c2, 1, true);
        CHECK(raw.at(63, 0) != raw.at(62, 0));   // edges preserved, not fixed
    }

    // --- packAlphaLayers: rebuild MCAL + MCLY offsets for 3 layers ----------
    {
        MapChunk a;
        a.layers.resize(3);
        std::vector<AlphaMap> maps(3);
        maps[1].texels.fill(255);   // layer 1 fully opaque
        for (int k = 0; k < 64 * 64; ++k)               // layer 2: a gradient
            maps[2].texels[k] = static_cast<uint8_t>(((k % 16) * 17));
        packAlphaLayers(a, maps, /*bigAlpha*/false);
        // Layer 0 = base, no alpha; layers 1,2 each 2048 B -> 4096 total.
        CHECK(!(a.layers[0].flags & MCLY_USE_ALPHA));
        CHECK(a.layers[1].flags & MCLY_USE_ALPHA);
        CHECK(a.layers[1].ofsAlpha == 0);
        CHECK(a.layers[2].ofsAlpha == 2048);
        CHECK(a.alpha.size() == 4096);
        CHECK(decodeAlphaMap(a, 1, false).at(20, 20) == 255);
        CHECK(decodeAlphaMap(a, 2, false).at(0, 15) == 15 * 17);
    }

    // --- MCLQ liquid parse --------------------------------------------------
    {
        // Build an MCNK flagged as river with one MCLQ layer: min/max + 81
        // verts (8 B each, height in the last 4) + 64 flag bytes.
        std::vector<uint8_t> hdr(128, 0);
        auto h32 = [&](size_t off, uint32_t v){ for(int i=0;i<4;i++) hdr[off+i]=(v>>(8*i))&0xFF; };
        h32(0x00, 0x0004);   // flags: MCNK_LQ_RIVER
        h32(0x0C, 0);        // nLayers

        std::vector<uint8_t> mclq;
        putf(mclq, 10.0f);   // minHeight
        putf(mclq, 20.0f);   // maxHeight
        for (int i = 0; i < 81; ++i) { put32(mclq, 0); putf(mclq, 15.0f); } // union + height
        for (int i = 0; i < 64; ++i) mclq.push_back(i == 0 ? 0x0F : 0x00);  // tile 0 = skip

        std::vector<uint8_t> body = hdr;
        chunk(body, "MCLQ", mclq);
        std::vector<uint8_t> adtL;
        chunk(adtL, "MCNK", body);

        MapChunk mcl = parseChunks(adtL)[0];
        CHECK(mcl.hasLiquid);
        CHECK(mcl.liquidType == LiquidType::River);
        CHECK_APPROX(mcl.liquid.minHeight, 10.0f);
        CHECK_APPROX(mcl.liquid.maxHeight, 20.0f);
        CHECK_APPROX(mcl.liquid.heights[40], 15.0f);
        CHECK(!liquidTileRenders(mcl.liquid.renderFlags[0]));  // 0x0F -> skip
        CHECK(liquidTileRenders(mcl.liquid.renderFlags[1]));   // 0x00 -> draw
    }

    // --- real vanilla MCNK layout: header offsets + uncounted MCNR padding ---
    // Reproduces the actual 1.12 file shape that a linear magic+size walk cannot
    // parse: MCNR declares 435 bytes but is followed by 13 pad bytes outside its
    // size, and the sub-chunks are located via the MCNK header offsets. A purely
    // iterative parser desyncs at MCNR and drops MCLY/MCAL; the offset path must
    // recover all of them.
    {
        std::vector<uint8_t> hdr(128, 0);
        auto h32 = [&](size_t off, uint32_t v){ for(int i=0;i<4;i++) hdr[off+i]=(v>>(8*i))&0xFF; };
        h32(0x0C, 2);          // nLayers (base + one alpha layer)

        std::vector<uint8_t> mcvt;
        for (int i = 0; i < 145; ++i) putf(mcvt, (float)i);

        std::vector<uint8_t> mcnr;                       // 435 bytes, no padding here
        // file order (x,y,z): up component is the last byte (matches real tiles).
        for (int i = 0; i < 145; ++i) { mcnr.push_back(0); mcnr.push_back(0); mcnr.push_back(127); }

        std::vector<uint8_t> mcly;                       // 2 layers * 16 bytes
        put32(mcly, 5); put32(mcly, 0);                  // layer0: base, no alpha
        put32(mcly, 0); put32(mcly, 0);
        put32(mcly, 6); put32(mcly, MCLY_USE_ALPHA);     // layer1: alpha-mapped
        put32(mcly, 0); put32(mcly, 0);                  // ofsAlpha=0, effectId=0

        std::vector<uint8_t> mcal(2048, 0xFF);           // one 4-bit map, all opaque

        // Assemble body = 128 header + sub-chunks, recording each sub-chunk's byte
        // offset within the body (== offset from MCNK data start) into the header.
        std::vector<uint8_t> body = hdr;
        h32(0x14, (uint32_t)body.size()); chunk(body, "MCVT", mcvt);
        h32(0x18, (uint32_t)body.size()); chunk(body, "MCNR", mcnr);
        for (int i = 0; i < 13; ++i) body.push_back(0);  // MCNR pad NOT in its size
        h32(0x1C, (uint32_t)body.size()); chunk(body, "MCLY", mcly);
        h32(0x24, (uint32_t)body.size());                // ofsMCAL
        h32(0x28, (uint32_t)mcal.size());                // sizeMCAL
        chunk(body, "MCAL", mcal);
        std::memcpy(body.data(), hdr.data(), 128);       // copy the finished header in

        std::vector<uint8_t> adtR;
        chunk(adtR, "MCNK", body);

        auto rc = parseChunks(adtR);
        CHECK(rc.size() == 1);
        const MapChunk& r = rc[0];
        CHECK_APPROX(r.heights[0],   0.0f);              // MCVT survived
        CHECK_APPROX(r.heights[144], 144.0f);
        CHECK(r.normals[0].z > 0.9f);                    // MCNR survived
        CHECK(r.layers.size() == 2);                     // MCLY survived the MCNR pad
        CHECK(r.layers[1].flags & MCLY_USE_ALPHA);
        CHECK(r.alpha.size() == 2048);                   // MCAL survived
        CHECK(decodeAlphaMap(r, 1, false).at(10, 10) == 255);
    }

    // --- MCRF doodad/object refs + MCSH shadow bitmap -----------------------
    {
        std::vector<uint8_t> hdr(128, 0);
        auto h32 = [&](size_t off, uint32_t v){ for(int i=0;i<4;i++) hdr[off+i]=(v>>(8*i))&0xFF; };
        h32(0x00, MCNK_HAS_MCSH);   // flags: shadow map present
        h32(0x10, 2);               // nDoodadRefs
        h32(0x38, 1);               // nMapObjRefs

        std::vector<uint8_t> mcrf;
        put32(mcrf, 11); put32(mcrf, 22);   // doodad (MDDF) indices
        put32(mcrf, 33);                     // map-object (MODF) index

        std::vector<uint8_t> mcsh(512, 0);
        mcsh[0] = 0x01;             // texel (0,0): bit 0
        mcsh[8] = 0x04;             // texel (1,2): bit index 66 -> byte 8, bit 2

        std::vector<uint8_t> body = hdr;
        chunk(body, "MCRF", mcrf);
        chunk(body, "MCSH", mcsh);
        std::vector<uint8_t> adtS;
        chunk(adtS, "MCNK", body);

        MapChunk mc = parseChunks(adtS)[0];
        CHECK(mc.doodadRefs.size() == 2 && mc.doodadRefs[0] == 11 && mc.doodadRefs[1] == 22);
        CHECK(mc.wmoRefs.size() == 1 && mc.wmoRefs[0] == 33);
        CHECK(mc.shadow.size() == 512);
        CHECK(shadowAt(mc, 0, 0));
        CHECK(shadowAt(mc, 1, 2));
        CHECK(!shadowAt(mc, 0, 1));
        CHECK(!shadowAt(mc, 63, 63));
        CHECK(!shadowAt(mc, 100, 0));   // out of range -> false
    }
}
