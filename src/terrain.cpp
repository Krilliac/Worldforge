#include "terrain.hpp"
#include "byte_reader.hpp"
#include "chunk.hpp"
#include "coords.hpp"

#include <algorithm>
#include <stdexcept>

namespace wf { static void parseMclq(MapChunk&, const uint8_t*, uint32_t); }

namespace wf {

// MCNK header field offsets (within the 128-byte header). Verified vs ADT/v18.
namespace {
constexpr size_t kHdrSize       = 128;
constexpr size_t kOffFlags      = 0x00;
constexpr size_t kOffIndexX     = 0x04;
constexpr size_t kOffIndexY     = 0x08;
constexpr size_t kOffNLayers    = 0x0C;
constexpr size_t kOffOfsMCVT    = 0x14;   // height map sub-chunk offset
constexpr size_t kOffOfsMCNR    = 0x18;   // normal map sub-chunk offset
constexpr size_t kOffOfsMCLY    = 0x1C;   // texture layer sub-chunk offset
constexpr size_t kOffOfsMCAL    = 0x24;   // alpha map sub-chunk offset
constexpr size_t kOffSizeMCAL   = 0x28;   // alpha map sub-chunk size
constexpr size_t kOffAreaId     = 0x34;
constexpr size_t kOffHoles      = 0x3C;
constexpr size_t kOffOfsMCLQ    = 0x60;   // liquid sub-chunk offset
constexpr size_t kOffPosition   = 0x68;

// Unpack one int8 MCNR triple to a Vec3 in world axes (x=north, y=west, z=up).
//
// The three bytes are stored in file order (b0, b1, b2) and map straight to
// (x, y, z); the up component is b2 (127 == 1.0). Verified against real vanilla
// tiles: flat ground stores (0, 0, 127), i.e. the up axis is the *last* byte.
// (An earlier reading treated the layout as X,Z,Y, which routed the up component
// into world Y -- that made flat chunks face sideways and self-shadow into the
// dark, flat, untextured-looking wedges seen in renders. See test_terrain.)
Vec3 unpackNormal(int8_t b0, int8_t b1, int8_t b2) {
    return normalize(Vec3{ b0 / 127.0f, b1 / 127.0f, b2 / 127.0f });
}
} // namespace

bool cellIsHole(uint16_t holes, int cellRow, int cellCol) {
    // The 16-bit map is a 4x4 grid; each bit covers a 2x2 block of the 8x8 cells.
    int holeBit = (cellRow / 2) * 4 + (cellCol / 2);
    return (holes & (1u << holeBit)) != 0;
}

static MapChunk parseOneChunk(const uint8_t* data, uint32_t size) {
    if (size < kHdrSize)
        throw std::runtime_error("MCNK smaller than its 128-byte header");

    MapChunk mc;
    ByteReader h(data, kHdrSize);
    h.seek(kOffFlags);    mc.flags  = h.u32();
    h.seek(kOffIndexX);   mc.indexX = h.u32();
    h.seek(kOffIndexY);   mc.indexY = h.u32();
    h.seek(kOffAreaId);   mc.areaId = h.u32();
    h.seek(kOffHoles);    mc.holes  = h.u16();
    h.seek(kOffPosition);
    mc.position = { h.f32(), h.f32(), h.f32() };

    auto u32At = [&](size_t off) -> uint32_t { ByteReader t(data, kHdrSize); t.seek(off); return t.u32(); };
    const uint32_t nLayers = u32At(kOffNLayers);
    const uint32_t ofsMCVT = u32At(kOffOfsMCVT);
    const uint32_t ofsMCNR = u32At(kOffOfsMCNR);
    const uint32_t ofsMCLY = u32At(kOffOfsMCLY);
    const uint32_t ofsMCAL = u32At(kOffOfsMCAL);
    const uint32_t szMCAL  = u32At(kOffSizeMCAL);
    const uint32_t ofsMCLQ = u32At(kOffOfsMCLQ);

    // Two ways to find the MCVT/MCNR/MCLY/MCAL/MCLQ sub-chunks:
    //
    //  (1) Real vanilla files pad MCNR with 13 bytes that its size field does NOT
    //      count, so a linear magic+size walk desyncs at MCNR and loses every
    //      sub-chunk after it. But those files always populate the MCNK header
    //      offsets, so we jump straight to each sub-chunk by offset -- immune to
    //      the padding. The offsets are documented ambiguously (relative to the
    //      MCNK chunk start vs. its data); detect the base from MCVT's magic.
    //
    //  (2) Some writers leave the header offsets zero. There we fall back to the
    //      linear walk, which is correct precisely because such files fold any
    //      MCNR padding into the declared size.
    const bool haveOffsets = ofsMCVT || ofsMCNR || ofsMCLY || ofsMCAL || ofsMCLQ;

    if (haveOffsets) {
        auto magicAt = [&](size_t pos) -> std::string {
            if (pos + 8 > size) return std::string();
            ByteReader t(data + pos, 8);
            return t.fourccReversed();
        };
        long baseAdj = 0;
        if (ofsMCVT && magicAt(ofsMCVT) != "MCVT" &&
            ofsMCVT >= 8 && magicAt(ofsMCVT - 8) == "MCVT")
            baseAdj = -8;

        // {payload, size} for the sub-chunk at header offset `ofs`, verifying its
        // magic. Size is taken from the sub-chunk header and clamped to the MCNK.
        auto sub = [&](uint32_t ofs, const char* expect) -> std::pair<const uint8_t*, uint32_t> {
            if (ofs == 0) return { nullptr, 0 };
            long p = static_cast<long>(ofs) + baseAdj;
            if (p < 0 || static_cast<size_t>(p) + 8 > size) return { nullptr, 0 };
            ByteReader t(data + p, 8);
            std::string m = t.fourccReversed();
            uint32_t sz   = t.u32();
            if (m != expect) return { nullptr, 0 };
            const size_t payload = static_cast<size_t>(p) + 8;
            if (payload + sz > size) sz = static_cast<uint32_t>(size - payload);
            return { data + payload, sz };
        };

        if (auto [p, sz] = sub(ofsMCVT, "MCVT"); p) {
            ByteReader r(p, sz);
            for (int i = 0; i < 145 && r.remaining() >= 4; ++i) mc.heights[i] = r.f32();
        }
        if (auto [p, sz] = sub(ofsMCNR, "MCNR"); p) {
            ByteReader r(p, sz);
            for (int i = 0; i < 145 && r.remaining() >= 3; ++i) {
                int8_t a  = static_cast<int8_t>(r.u8());
                int8_t b  = static_cast<int8_t>(r.u8());
                int8_t cc = static_cast<int8_t>(r.u8());
                mc.normals[i] = unpackNormal(a, b, cc);
            }
        }
        if (auto [p, sz] = sub(ofsMCLY, "MCLY"); p) {
            ByteReader r(p, sz);
            for (uint32_t i = 0; i < nLayers && r.remaining() >= 16; ++i) {
                TexLayer l;
                l.textureId = r.u32();
                l.flags     = r.u32();
                l.ofsAlpha  = r.u32();
                l.effectId  = r.u32();
                mc.layers.push_back(l);
            }
        }
        if (auto [p, sz] = sub(ofsMCAL, "MCAL"); p) {
            uint32_t n = std::min(sz, szMCAL ? szMCAL : sz);
            mc.alpha.assign(p, p + n);
        }
        if (auto [p, sz] = sub(ofsMCLQ, "MCLQ"); p)
            parseMclq(mc, p, sz);

        return mc;
    }

    // Fallback: linear sub-chunk walk for files with no header offsets.
    const uint8_t* subData = data + kHdrSize;
    size_t subLen = size - kHdrSize;
    forEachChunk(subData, subLen, [&](const Chunk& c) {
        if (c.magic == "MCVT") {
            ByteReader r(c.data, c.size);
            for (int i = 0; i < 145 && r.remaining() >= 4; ++i) mc.heights[i] = r.f32();
        } else if (c.magic == "MCNR") {
            ByteReader r(c.data, c.size);
            for (int i = 0; i < 145 && r.remaining() >= 3; ++i) {
                int8_t a  = static_cast<int8_t>(r.u8());
                int8_t b  = static_cast<int8_t>(r.u8());
                int8_t cc = static_cast<int8_t>(r.u8());
                mc.normals[i] = unpackNormal(a, b, cc);
            }
        } else if (c.magic == "MCLY") {
            ByteReader r(c.data, c.size);
            while (r.remaining() >= 16) {
                TexLayer l;
                l.textureId = r.u32();
                l.flags     = r.u32();
                l.ofsAlpha  = r.u32();
                l.effectId  = r.u32();
                mc.layers.push_back(l);
            }
        } else if (c.magic == "MCAL") {
            mc.alpha.assign(c.data, c.data + c.size);
        } else if (c.magic == "MCLQ") {
            parseMclq(mc, c.data, c.size);
        }
        return true;
    });
    return mc;
}

AlphaMap decodeAlphaMap(const MapChunk& mc, size_t layerIndex, bool bigAlpha) {
    AlphaMap map;
    constexpr int N = AlphaMap::DIM * AlphaMap::DIM;   // 4096 texels

    // Layer 0 is the opaque base; nothing blends beneath it.
    if (layerIndex == 0) {
        map.texels.fill(255);
        return map;
    }
    if (layerIndex >= mc.layers.size())
        return map;   // no such layer -> transparent

    const TexLayer& layer = mc.layers[layerIndex];
    if (!(layer.flags & MCLY_USE_ALPHA))
        return map;   // layer declares no alpha map -> transparent

    const std::vector<uint8_t>& blob = mc.alpha;
    const size_t ofs = layer.ofsAlpha;

    if (layer.flags & MCLY_COMPRESSED) {
        // RLE: each command byte is [fill:1][count:7]. Fill repeats the next
        // byte `count` times; copy emits the next `count` bytes verbatim.
        size_t in = ofs, out = 0;
        while (out < N && in < blob.size()) {
            uint8_t cmd = blob[in++];
            int count   = cmd & 0x7F;
            if (cmd & 0x80) {                        // fill
                uint8_t val = (in < blob.size()) ? blob[in++] : 0;
                while (count-- > 0 && out < N) map.texels[out++] = val;
            } else {                                 // copy
                while (count-- > 0 && out < N && in < blob.size())
                    map.texels[out++] = blob[in++];
            }
        }
        return map;
    }

    if (bigAlpha) {
        // One byte per texel, straight copy.
        for (int k = 0; k < N; ++k) {
            size_t bi = ofs + static_cast<size_t>(k);
            map.texels[k] = (bi < blob.size()) ? blob[bi] : 0;
        }
        return map;
    }

    // Vanilla packed 4-bit: two texels per byte, low nibble first. Scale a
    // 0..15 nibble to 0..255 via *17 (15*17 == 255), so full coverage is exact.
    for (int k = 0; k < N; ++k) {
        size_t bi = ofs + static_cast<size_t>(k) / 2;
        uint8_t byte = (bi < blob.size()) ? blob[bi] : 0;
        uint8_t nib  = (k & 1) ? (byte >> 4) : (byte & 0x0F);
        map.texels[k] = static_cast<uint8_t>(nib * 17);
    }
    return map;
}

std::vector<uint8_t> encodeAlphaMap(const AlphaMap& map, bool bigAlpha) {
    constexpr int N = AlphaMap::DIM * AlphaMap::DIM;   // 4096 texels
    std::vector<uint8_t> out;
    if (bigAlpha) {
        out.assign(map.texels.begin(), map.texels.end());   // 4096 bytes
        return out;
    }
    out.assign(N / 2, 0);                                   // 2048 bytes
    for (int k = 0; k < N; ++k) {
        // 0..255 -> nearest 0..15 (round, not truncate): (v + 8) / 17.
        uint8_t nib = static_cast<uint8_t>((map.texels[k] + 8) / 17);
        if (nib > 15) nib = 15;
        if (k & 1) out[k / 2] |= static_cast<uint8_t>(nib << 4);  // high nibble
        else       out[k / 2] |= nib;                            // low nibble
    }
    return out;
}

void packAlphaLayers(MapChunk& mc, const std::vector<AlphaMap>& maps, bool bigAlpha) {
    mc.alpha.clear();
    const size_t n = std::min(mc.layers.size(), maps.size());
    for (size_t i = 0; i < n; ++i) {
        TexLayer& layer = mc.layers[i];
        layer.flags &= ~MCLY_COMPRESSED;           // we never emit RLE
        if (i == 0) {                              // base layer: no alpha
            layer.flags &= ~MCLY_USE_ALPHA;
            layer.ofsAlpha = 0;
            continue;
        }
        layer.flags |= MCLY_USE_ALPHA;
        layer.ofsAlpha = static_cast<uint32_t>(mc.alpha.size());
        std::vector<uint8_t> enc = encodeAlphaMap(maps[i], bigAlpha);
        mc.alpha.insert(mc.alpha.end(), enc.begin(), enc.end());
    }
}

// Parse one MCLQ liquid layer (min/max height, 9x9 vertex grid, 8x8 flags).
static void parseMclq(MapChunk& mc, const uint8_t* data, uint32_t size) {
    LiquidType t = LiquidType::None;
    if      (mc.flags & MCNK_LQ_RIVER) t = LiquidType::River;
    else if (mc.flags & MCNK_LQ_OCEAN) t = LiquidType::Ocean;
    else if (mc.flags & MCNK_LQ_MAGMA) t = LiquidType::Magma;
    else if (mc.flags & MCNK_LQ_SLIME) t = LiquidType::Slime;

    ByteReader r(data, size);
    // 2 floats + 81 verts * 8 bytes + 64 flag bytes = 720 bytes per layer.
    if (r.remaining() < 8u + 81u * 8u + 64u) return;

    const bool isWater = (t == LiquidType::River || t == LiquidType::Ocean);

    MclqLayer L;
    L.minHeight = r.f32();
    L.maxHeight = r.f32();
    for (int i = 0; i < 81; ++i) {
        // The 4-byte union is water {depth,flow0,flow1,filler} or magma {x,y}.
        // For water the first byte is the depth (0..255) that drives shoreline
        // transparency; magma/slime store texcoords we don't need here.
        uint8_t b0 = r.u8();
        r.skip(3);                // remaining union bytes (flow / texcoord tail)
        L.depth[i]   = isWater ? b0 : 0;
        L.heights[i] = r.f32();   // height is always the last 4 bytes
    }
    for (int i = 0; i < 64; ++i) L.renderFlags[i] = r.u8();

    mc.hasLiquid  = true;
    mc.liquidType = t;
    mc.liquid     = L;
}

std::vector<MapChunk> parseChunks(const std::vector<uint8_t>& adtBuf) {
    std::vector<MapChunk> out;
    out.reserve(256);
    forEachChunk(adtBuf.data(), adtBuf.size(), [&](const Chunk& c) {
        if (c.magic == "MCNK") out.push_back(parseOneChunk(c.data, c.size));
        return true;
    });
    return out;
}

Mesh buildChunkMesh(const MapChunk& mc, int blockX, int blockY) {
    Mesh mesh;
    mesh.vertices.resize(145);

    // The MCNK header carries IndexX/IndexY; the row/col -> north/west axis
    // assignment is the one piece to confirm against a real tile (same risk
    // class as WDT x/y order). We treat indexX as the west-east column and
    // indexY as the north-south row, and use the header height base directly.
    const int   col  = static_cast<int>(mc.indexX);   // west-east
    const int   row  = static_cast<int>(mc.indexY);   // north-south
    const Vec3  corner = chunkCornerWorld(blockX, blockY, row, col, mc.position.z);
    const float U = static_cast<float>(UNIT_SIZE);

    auto outerBufIdx = [](int i, int j) { return i * 9 + j; };       // 0..80
    auto innerBufIdx = [](int i, int j) { return 81 + i * 8 + j; };  // 81..144
    auto outerMcvt   = [](int i, int j) { return i * 17 + j; };
    auto innerMcvt   = [](int i, int j) { return i * 17 + 9 + j; };

    // Outer 9x9: i = north-south (0 = north edge), j = west-east (0 = west edge).
    for (int i = 0; i < 9; ++i) {
        for (int j = 0; j < 9; ++j) {
            int b = outerBufIdx(i, j);
            int m = outerMcvt(i, j);
            mesh.vertices[b].position = {
                corner.x - i * U,                 // X north: south -> lower
                corner.y - j * U,                 // Y west:  east  -> lower
                mc.position.z + mc.heights[m]
            };
            mesh.vertices[b].normal = mc.normals[m];
        }
    }
    // Inner 8x8: centred half a cell into each quad.
    for (int i = 0; i < 8; ++i) {
        for (int j = 0; j < 8; ++j) {
            int b = innerBufIdx(i, j);
            int m = innerMcvt(i, j);
            mesh.vertices[b].position = {
                corner.x - (i + 0.5f) * U,
                corner.y - (j + 0.5f) * U,
                mc.position.z + mc.heights[m]
            };
            mesh.vertices[b].normal = mc.normals[m];
        }
    }

    // Four triangles per inner cell, fanned around the centre vertex.
    mesh.indices.reserve(8 * 8 * 4 * 3);
    for (int i = 0; i < 8; ++i) {
        for (int j = 0; j < 8; ++j) {
            if (cellIsHole(mc.holes, i, j)) continue;
            uint32_t C  = static_cast<uint32_t>(innerBufIdx(i, j));
            uint32_t TL = static_cast<uint32_t>(outerBufIdx(i,     j));
            uint32_t TR = static_cast<uint32_t>(outerBufIdx(i,     j + 1));
            uint32_t BL = static_cast<uint32_t>(outerBufIdx(i + 1, j));
            uint32_t BR = static_cast<uint32_t>(outerBufIdx(i + 1, j + 1));
            // Consistent winding around the centre.
            mesh.indices.insert(mesh.indices.end(), { C, TL, TR });
            mesh.indices.insert(mesh.indices.end(), { C, TR, BR });
            mesh.indices.insert(mesh.indices.end(), { C, BR, BL });
            mesh.indices.insert(mesh.indices.end(), { C, BL, TL });
        }
    }
    return mesh;
}

Rgba liquidTint(LiquidType type) {
    switch (type) {
        case LiquidType::River: return Rgba{  40, 110, 180, 140 };  // translucent blue
        case LiquidType::Ocean: return Rgba{  30,  90, 160, 150 };  // deeper blue
        case LiquidType::Magma: return Rgba{ 235, 110,  25, 235 };  // emissive orange
        case LiquidType::Slime: return Rgba{ 120, 175,  45, 220 };  // murky green
        case LiquidType::None:  break;
    }
    return Rgba{ 0, 0, 0, 0 };
}

Mesh buildLiquidMesh(const MapChunk& mc, int blockX, int blockY) {
    Mesh mesh;
    if (!mc.hasLiquid) return mesh;

    const MclqLayer& L = mc.liquid;

    // Same outer-grid footprint as buildChunkMesh: indexX = west-east column,
    // indexY = north-south row. Only Z changes -- use the liquid height.
    const int   col    = static_cast<int>(mc.indexX);   // west-east
    const int   row    = static_cast<int>(mc.indexY);   // north-south
    const Vec3  corner = chunkCornerWorld(blockX, blockY, row, col, mc.position.z);
    const float U      = static_cast<float>(UNIT_SIZE);

    auto gridIdx = [](int i, int j) { return i * 9 + j; };   // 0..80, row-major

    // 9x9 surface vertices: world XY identical to the terrain outer ring, Z from
    // the stored liquid height (clamped to the layer's [min,max] envelope).
    mesh.vertices.resize(81);
    for (int i = 0; i < 9; ++i) {
        for (int j = 0; j < 9; ++j) {
            int idx = gridIdx(i, j);
            float z = L.heights[idx];
            if (z < L.minHeight) z = L.minHeight;
            if (z > L.maxHeight) z = L.maxHeight;
            mesh.vertices[idx].position = {
                corner.x - i * U,   // X north: south -> lower
                corner.y - j * U,   // Y west:  east  -> lower
                z
            };
            mesh.vertices[idx].normal = { 0.0f, 0.0f, 1.0f };  // flat surface
        }
    }

    // Two triangles per rendered 8x8 cell; skip cells the MCLQ mask flags as
    // "don't render" (low nibble 0xF), so water covers only the wet cells.
    mesh.indices.reserve(8 * 8 * 2 * 3);
    for (int i = 0; i < 8; ++i) {
        for (int j = 0; j < 8; ++j) {
            if (!liquidTileRenders(L.renderFlags[i * 8 + j])) continue;
            uint32_t TL = static_cast<uint32_t>(gridIdx(i,     j));
            uint32_t TR = static_cast<uint32_t>(gridIdx(i,     j + 1));
            uint32_t BL = static_cast<uint32_t>(gridIdx(i + 1, j));
            uint32_t BR = static_cast<uint32_t>(gridIdx(i + 1, j + 1));
            mesh.indices.insert(mesh.indices.end(), { TL, TR, BR });
            mesh.indices.insert(mesh.indices.end(), { TL, BR, BL });
        }
    }
    return mesh;
}

Mesh buildTileMesh(const std::vector<MapChunk>& chunks, int blockX, int blockY) {
    Mesh tile;
    for (const MapChunk& mc : chunks) {
        Mesh m = buildChunkMesh(mc, blockX, blockY);
        uint32_t base = static_cast<uint32_t>(tile.vertices.size());
        tile.vertices.insert(tile.vertices.end(), m.vertices.begin(), m.vertices.end());
        for (uint32_t idx : m.indices) tile.indices.push_back(base + idx);
    }
    return tile;
}

} // namespace wf
