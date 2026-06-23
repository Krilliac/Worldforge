#pragma once
// ---------------------------------------------------------------------------
// Terrain: parse MCNK map chunks (header + MCVT heights, MCNR normals, MCLY
// texture layers, low-res holes) and assemble a renderable triangle mesh in
// world space. Vanilla v18 layout, verified against wowdev.wiki ADT/v18.
//
// Each MCNK is a 9x9 outer grid interleaved with an 8x8 inner grid (145 verts).
// Rendering uses the inner vertex as the centre of a 4-triangle fan to its four
// surrounding outer vertices -- the scheme the client itself uses.
// ---------------------------------------------------------------------------
#include <array>
#include <cstdint>
#include <vector>
#include "math.hpp"

namespace wf {

// MCLY layer-flag bits we act on.
constexpr uint32_t MCLY_USE_ALPHA  = 0x100;  // layer has a map in MCAL
constexpr uint32_t MCLY_COMPRESSED = 0x200;  // that map is RLE-compressed

struct TexLayer {              // MCLY entry (16 bytes)
    uint32_t textureId = 0;    // index into the ADT's MTEX list
    uint32_t flags     = 0;    // 0x100 use_alpha_map, 0x200 compressed, ...
    uint32_t ofsAlpha  = 0;    // offset of this layer's map into MCAL
    uint32_t effectId  = 0;    // ground-effect / detail-doodad id
};

struct MapChunk {
    uint32_t flags   = 0;
    uint32_t indexX  = 0;      // header IndexX (see terrain.cpp note on axis map)
    uint32_t indexY  = 0;      // header IndexY
    uint32_t areaId  = 0;
    uint16_t holes   = 0;      // low-res 4x4 hole bitmap
    Vec3     position;         // MCNK header position; .z is the height base
    std::array<float, 145> heights{};   // MCVT, relative to position.z
    std::array<Vec3,  145> normals{};   // MCNR, unpacked to unit-ish vectors
    std::vector<TexLayer> layers;       // MCLY
    std::vector<uint8_t>  alpha;        // raw MCAL blob (unpack deferred to texturing)
};

struct Vertex {
    Vec3 position;   // world space
    Vec3 normal;
};

struct Mesh {
    std::vector<Vertex>   vertices;
    std::vector<uint32_t> indices;   // triangle list
};

// A decoded per-layer alpha (blend) coverage map: 64x64 texels, one byte each,
// 0 = this layer fully transparent, 255 = fully opaque over the layers below.
struct AlphaMap {
    static constexpr int DIM = 64;
    std::array<uint8_t, DIM * DIM> texels{};
    uint8_t at(int row, int col) const {
        return texels[static_cast<size_t>(row) * DIM + col];
    }
};

// Decode the blend map for layer `layerIndex` of a chunk out of its MCAL blob.
//
// Layer 0 is the base coat and carries no alpha (always fully opaque). For other
// layers the encoding depends on the WDT's MPHD ADT_HAS_BIG_ALPHA flag, passed
// as `bigAlpha`:
//   * compressed (MCLY flag 0x200) -> RLE stream, regardless of bigAlpha;
//   * bigAlpha == true             -> 4096 bytes, one 8-bit texel each;
//   * bigAlpha == false (vanilla)  -> 2048 bytes, two packed 4-bit texels each,
//                                     scaled 0..15 -> 0..255.
// Out-of-range offsets or truncated data decode to transparent (0) rather than
// reading past the blob.
AlphaMap decodeAlphaMap(const MapChunk& mc, size_t layerIndex, bool bigAlpha);

// Parse all 256 MCNK chunks from a full ADT buffer.
// blockX/blockY are the tile's WDT indices, needed for world placement.
std::vector<MapChunk> parseChunks(const std::vector<uint8_t>& adtBuf);

// Build a single chunk's world-space mesh (hole-aware).
Mesh buildChunkMesh(const MapChunk& mc, int blockX, int blockY);

// Convenience: build one merged mesh for an entire tile.
Mesh buildTileMesh(const std::vector<MapChunk>& chunks, int blockX, int blockY);

// True if inner cell (cellRow,cellCol) in 0..7 lies in a low-res hole.
bool cellIsHole(uint16_t holes, int cellRow, int cellCol);

} // namespace wf
