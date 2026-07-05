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
#include "image.hpp"   // Rgba

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

// MCNK header flag bits we act on (ADT/v18).
constexpr uint32_t MCNK_HAS_MCSH         = 0x0001;
constexpr uint32_t MCNK_IMPASSABLE       = 0x0002;
constexpr uint32_t MCNK_LQ_RIVER         = 0x0004;
constexpr uint32_t MCNK_LQ_OCEAN         = 0x0008;
constexpr uint32_t MCNK_LQ_MAGMA         = 0x0010;
constexpr uint32_t MCNK_LQ_SLIME         = 0x0020;
constexpr uint32_t MCNK_DO_NOT_FIX_ALPHA = 0x8000;
// WoW 5.3+ only: the 8 bytes at header +0x14 (vanilla ofsMCVT/ofsMCNR) are an
// 8x8 high-res hole bitmap, NOT sub-chunk offsets. We never write this flag;
// the parser refuses such chunks rather than dereference bitmap bits.
constexpr uint32_t MCNK_HIGH_RES_HOLES   = 0x10000;

// Liquid category, derived from the MCNK header liquid flags.
enum class LiquidType : uint32_t { None = 0, River, Ocean, Magma, Slime };

// One MCLQ liquid layer: a 9x9 height grid over the chunk plus an 8x8 render
// mask. The per-vertex depth/flow (water) or texture-coord (magma) union is not
// retained; only the height (the last 4 bytes of each 8-byte vertex) is kept,
// which is all the mesh/water surface needs. A chunk can stack several layers
// (e.g. a river surface over an ocean floor): vanilla stores one 804-byte block
// per LQ flag set in the MCNK header, in flag order river/ocean/magma/slime.
struct MclqLayer {
    LiquidType type = LiquidType::None;    // which LQ flag this block belongs to
    float minHeight = 0.0f;
    float maxHeight = 0.0f;
    std::array<float, 81>  heights{};      // 9x9 outer grid
    std::array<uint8_t, 81> depth{};       // per-vertex water depth (0..255); 0 for magma/slime
    std::array<uint8_t, 64> renderFlags{}; // 8x8 tiles; low nibble 0xF == skip
};

// True if an MCLQ 8x8 tile flag indicates the tile should be drawn.
inline bool liquidTileRenders(uint8_t flag) { return (flag & 0x0F) != 0x0F; }

// One MCSE sound emitter. The 1.12.1 record is 52 bytes: uint32 soundPointID,
// uint32 soundNameID, float pos[3] (at +0x08), float min/max/cutoff distance,
// then a 20-byte timing/count tail (uint16 startTime, endTime, mode; uint8
// loopCountMin, loopCountMax; uint16 groupSilenceMin/Max, playInstancesMin/Max,
// interSoundGapMin/Max) that is skipped over but not kept.
struct SoundEmitter {
    uint32_t soundPointID   = 0;   // record field 0
    uint32_t soundNameID    = 0;   // record field 1
    uint32_t soundId        = 0;   // legacy alias of soundPointID (kept source-compatible)
    Vec3     position;             // emitter position (record offset 0x08)
    float    minDistance    = 0.0f;
    float    maxDistance    = 0.0f;
    float    cutoffDistance = 0.0f;
};

// Base translucent tint for a liquid category (RGBA). Water/ocean are a
// semi-transparent blue; magma is a near-opaque emissive orange; slime a murky
// green. These are deliberate placeholder tints (the real client derives them
// from Light*Band.dbc water-color curves) -- enough to read the surface offline.
Rgba liquidTint(LiquidType type);

// True for liquids that glow (magma/slime): the render path skips directional
// shading so they read as emissive rather than going dark on shadowed slopes.
inline bool liquidEmissive(LiquidType type) {
    return type == LiquidType::Magma || type == LiquidType::Slime;
}

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
    std::vector<uint8_t>  alpha;        // raw MCAL blob (decode via decodeAlphaMap)
    std::vector<uint8_t>  shadow;       // raw MCSH 64x64 shadow bitmap (512 B); empty if absent
    std::vector<uint32_t> doodadRefs;   // MCRF: indices into the ADT's MDDF doodad list
    std::vector<uint32_t> wmoRefs;      // MCRF: indices into the ADT's MODF map-object list
    std::vector<SoundEmitter> soundEmitters;  // MCSE: per-chunk sound emitters

    bool       hasLiquid  = false;      // MCLQ present
    LiquidType liquidType = LiquidType::None;   // first layer's type
    MclqLayer  liquid;                  // FIRST liquid layer (back-compat view)
    std::vector<MclqLayer> liquidLayers;  // ALL MCLQ layers, in LQ-flag order

    // MCNK header +0x40: 'ReallyLowQualityTextureingMap' (predTex), 8x8 cells x
    // 2 bits each = the MCLY layer index whose effectId spawns ground-effect
    // doodads in that cell; +0x50: noEffectDoodad, 8x8 x 1 bit suppression mask.
    // Kept in their packed on-disk form (decode via decodePredTex).
    std::array<uint8_t, 16> predTex{};
    std::array<uint8_t, 8>  noEffectDoodad{};

    // Absolute byte offsets of this chunk's MCVT height floats / MCNR normal
    // bytes within the source ADT buffer parseChunks() read (0 if absent). Let
    // writeAdtHeights() / writeAdtNormals() patch edits back in place without
    // rewriting the rest of the file. Not part of the rendered model.
    uint32_t   mcvtOffset = 0;
    uint32_t   mcnrOffset = 0;
    // Same idea for the 128-byte MCNK header itself (lets writeAdtHoles /
    // writeAdtAreaIds / writeAdtChunkFlags / writeAdtPredTex poke header fields
    // in place) and for the MCSH shadow payload. 0 if unknown/absent.
    uint32_t   mcnkHeaderOffset = 0;
    uint32_t   mcshOffset = 0;
};

// Sample the MCSH shadow bitmap at (row, col) in [0,64): true == the texel is in
// baked terrain shadow. The 64x64 bits are row-major, LSB-first within each byte.
// Returns false when the chunk carries no shadow map (out-of-range too).
bool shadowAt(const MapChunk& mc, int row, int col);

// Resolve a chunk's MCRF reference indices (mc.doodadRefs / mc.wmoRefs) into
// pointers to the referenced ADT placement entries (MDDF DoodadDef / MODF
// WmoDef, from the parsed Adt). Out-of-range indices are skipped. MCRF is how a
// chunk names *which* of the tile's placements fall in it -- the basis for
// per-chunk culling / "what's in this chunk" selection, rather than testing
// every tile placement against every chunk. Generic so it serves both ref lists
// without coupling terrain.hpp to the ADT placement types.
template <class Def>
std::vector<const Def*> resolveChunkRefs(const std::vector<uint32_t>& refs,
                                         const std::vector<Def>& defs) {
    std::vector<const Def*> out;
    out.reserve(refs.size());
    for (uint32_t i : refs)
        if (i < defs.size()) out.push_back(&defs[i]);
    return out;
}

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

// Apply the client's implicit 63->64 alpha edge fix: an uncompressed MCAL map
// stores no real data in its last row/column, so the client duplicates the
// previous one (row/col 63 := 62) to avoid a hard seam at the chunk border.
// This is a DRAW-time fix, deliberately kept out of decodeAlphaMap (which stays
// a loss-less codec for the editor write path); the renderer applies it when
// building splat layers, and only when the chunk lacks MCNK_DO_NOT_FIX_ALPHA.
void fixAlphaMapEdges(AlphaMap& map);

// Encode a single coverage map back to its MCAL byte form (the inverse of
// decodeAlphaMap, for an editor's texture-paint write path). bigAlpha selects
// the 8-bit (4096 B) vs packed 4-bit (2048 B) form. The 4-bit form quantises
// 0..255 -> nearest multiple of 17, so a decode->encode->decode round-trip is
// exact only for values that are already multiples of 17.
std::vector<uint8_t> encodeAlphaMap(const AlphaMap& map, bool bigAlpha);

// Rebuild a chunk's MCAL blob from per-layer coverage maps and refresh each
// MCLY entry's use-alpha flag + ofsAlpha. maps[i] pairs with mc.layers[i];
// layer 0 (the base) carries no alpha and is skipped. Drops any compression.
void packAlphaLayers(MapChunk& mc, const std::vector<AlphaMap>& maps, bool bigAlpha);

// Parse all 256 MCNK chunks from a full ADT buffer.
// blockX/blockY are the tile's WDT indices, needed for world placement.
std::vector<MapChunk> parseChunks(const std::vector<uint8_t>& adtBuf);

// Patch a tile's terrain heights back into its ADT bytes. `chunks` must come
// from parseChunks(adtBuf) (same buffer, so their mcvtOffset values index into
// it); each chunk's 145 heights are written as little-endian floats over the
// original MCVT data. Returns a new buffer that is byte-identical to `adtBuf`
// except for the patched heights -- the surgical, verifiable inverse of the
// height read path (every other sub-chunk, header and offset is untouched, so
// the output is a valid ADT). Chunks with mcvtOffset == 0 are skipped. Throws if
// an offset + 580 bytes runs past the buffer (a sign `chunks` and `adtBuf` don't
// match). This intentionally exports ONLY height edits; placements/textures are
// authored through other paths.
std::vector<uint8_t> writeAdtHeights(const std::vector<uint8_t>& adtBuf,
                                     const std::vector<MapChunk>& chunks);

// Patch a tile's vertex normals back into its ADT bytes, the MCNR counterpart of
// writeAdtHeights. Each chunk's 145 unit normals are quantised to the file's
// int8 triples (component * 127, file order x,y,z) and written over the original
// MCNR data; the 13 trailing pad bytes vanilla appends are left untouched. All
// other bytes are byte-identical. Chunks with mcnrOffset == 0 are skipped.
// Throws if an offset + 435 bytes runs past the buffer.
std::vector<uint8_t> writeAdtNormals(const std::vector<uint8_t>& adtBuf,
                                     const std::vector<MapChunk>& chunks);

// MCNK-header patchers, the surgical siblings of writeAdtHeights: each returns
// a copy of adtBuf with ONE header field poked per chunk -- located through
// mc.mcnkHeaderOffset (chunks with offset 0 are skipped) -- and every other
// byte untouched. Throws if a header runs past the buffer (a sign `chunks` and
// `adtBuf` don't match).
//   writeAdtHoles      holes            -> header+0x3C (uint16)
//   writeAdtAreaIds    areaId           -> header+0x34 (uint32)
//   writeAdtChunkFlags flags            -> header+0x00 (uint32; this is how the
//                                          impassable bit MCNK_IMPASSABLE 0x2
//                                          is painted onto a chunk)
//   writeAdtPredTex    predTex (16 B) + noEffectDoodad (8 B) -> header+0x40..0x57
std::vector<uint8_t> writeAdtHoles(const std::vector<uint8_t>& adtBuf,
                                   const std::vector<MapChunk>& chunks);
std::vector<uint8_t> writeAdtAreaIds(const std::vector<uint8_t>& adtBuf,
                                     const std::vector<MapChunk>& chunks);
std::vector<uint8_t> writeAdtChunkFlags(const std::vector<uint8_t>& adtBuf,
                                        const std::vector<MapChunk>& chunks);
std::vector<uint8_t> writeAdtPredTex(const std::vector<uint8_t>& adtBuf,
                                     const std::vector<MapChunk>& chunks);

// Decode the MCNK header's 16-byte predTex map to one byte per 8x8 cell (value
// 0..3 = MCLY layer index), row-major, 4 cells per byte packed LSB-first (cell
// k lives in bits (k%4)*2 .. (k%4)*2+1 of byte k/4). encodePredTex is the exact
// inverse; the pair is loss-less both ways (every 2-bit field maps to one cell
// and back), so a decode->encode round-trip is byte-identical.
std::array<uint8_t, 64> decodePredTex(const uint8_t* packed);   // packed: 16 bytes
std::array<uint8_t, 16> encodePredTex(const std::array<uint8_t, 64>& cells);

// Recompute every chunk's MCNR vertex normals from the (possibly edited) MCVT
// height field of the whole tile. Used after a terrain sculpt so lighting tracks
// the new slopes. Normals come from central differences over the tile's shared
// 129x129 outer-vertex grid -- sampling neighbouring chunks across their common
// edge -- so a vertex shared by two chunks gets one identical normal in both and
// the lighting stays seamless. Inner (half-cell) normals are the mean of their
// four surrounding outer normals. Tile-boundary vertices fall back to one-sided
// differences. `chunks` are located by their MCNK IndexX/IndexY, so the order in
// the vector doesn't matter. UNIT_SIZE spacing; no allocation of the source.
void recomputeTileNormals(std::vector<MapChunk>& chunks);

// Build a single chunk's world-space mesh (hole-aware).
Mesh buildChunkMesh(const MapChunk& mc, int blockX, int blockY);

// Build a chunk's liquid SURFACE mesh from its MCLQ layer: a 9x9 vertex grid at
// the stored liquid heights, in the same world-XY footprint as buildChunkMesh
// (only Z differs -- the liquid height, clamped to [minHeight,maxHeight]). Each
// of the 64 8x8 cells emits two triangles ONLY when liquidTileRenders() is true
// (the "don't render" mask is skipped), so dry land carries no water quads.
// Vertex normals are +Z (a flat surface). Returns an empty mesh when the chunk
// has no liquid. This is a separate, translucent pass -- never the opaque path.
Mesh buildLiquidMesh(const MapChunk& mc, int blockX, int blockY);

// Convenience: build one merged mesh for an entire tile.
Mesh buildTileMesh(const std::vector<MapChunk>& chunks, int blockX, int blockY);

// True if inner cell (cellRow,cellCol) in 0..7 lies in a low-res hole.
bool cellIsHole(uint16_t holes, int cellRow, int cellCol);

// Editor write path for the low-res 4x4 hole grid: set/clear one bit at
// (subX = west-east sub-column, subY = north-south sub-row), both 0..3; the
// bit index is subY*4 + subX from the LSB. Each bit voids a 2x2 block of the
// 8x8 render cells -- exactly the mapping cellIsHole() reads, so
// cellIsHole(mc.holes, subY*2, subX*2) flips with the bit. Out-of-range
// coordinates are ignored. Persist edits with writeAdtHoles().
void setHoleBit(MapChunk& mc, int subX, int subY);
void clearHoleBit(MapChunk& mc, int subX, int subY);
void setAllHoles(MapChunk& mc);    // holes = 0xFFFF: the whole chunk is void
void clearAllHoles(MapChunk& mc);  // holes = 0: solid ground

} // namespace wf
