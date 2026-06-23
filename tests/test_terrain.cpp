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

    // MCNR: 145 * int8[3] (X,Z,Y) all pointing straight up (0,127,0) + 13 pad.
    std::vector<uint8_t> mcnr;
    for (int i = 0; i < 145; ++i) { mcnr.push_back(0); mcnr.push_back(127); mcnr.push_back(0); }
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
    // Normal (0,127,0) stored as X,Z,Y -> world up (+Z).
    CHECK_APPROX(mc.normals[0].z, 1.0f);
    CHECK_APPROX(mc.normals[0].x, 0.0f);

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
}
