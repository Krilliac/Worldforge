#include "terrain.hpp"
#include "byte_reader.hpp"
#include "chunk.hpp"
#include "coords.hpp"
#include "editing.hpp"   // Falloff / falloffWeight, for paintShadow

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace wf {
static void parseMclq(MapChunk&, const uint8_t*, uint32_t);
static void parseMcrf(MapChunk&, const uint8_t*, uint32_t, uint32_t nDoodad, uint32_t nWmo);
static void parseMcsh(MapChunk&, const uint8_t*, uint32_t);
static void parseMcse(MapChunk&, const uint8_t*, uint32_t, uint32_t nSndEmitters);
}

namespace wf {

// MCNK header field offsets (within the 128-byte header). Verified vs ADT/v18.
namespace {
constexpr size_t kHdrSize       = 128;
constexpr size_t kOffFlags      = 0x00;
constexpr size_t kOffIndexX     = 0x04;
constexpr size_t kOffIndexY     = 0x08;
constexpr size_t kOffNLayers    = 0x0C;
constexpr size_t kOffNDoodadRefs= 0x10;   // count of MCRF doodad (MDDF) indices
constexpr size_t kOffOfsMCVT    = 0x14;   // height map sub-chunk offset
constexpr size_t kOffOfsMCNR    = 0x18;   // normal map sub-chunk offset
constexpr size_t kOffOfsMCLY    = 0x1C;   // texture layer sub-chunk offset
constexpr size_t kOffOfsMCRF    = 0x20;   // doodad/object reference sub-chunk offset
constexpr size_t kOffOfsMCAL    = 0x24;   // alpha map sub-chunk offset
constexpr size_t kOffSizeMCAL   = 0x28;   // alpha map sub-chunk size
constexpr size_t kOffOfsMCSH    = 0x2C;   // shadow map sub-chunk offset
constexpr size_t kOffSizeMCSH   = 0x30;   // shadow map sub-chunk size
constexpr size_t kOffAreaId     = 0x34;
constexpr size_t kOffNMapObjRefs= 0x38;   // count of MCRF map-object (MODF) indices
constexpr size_t kOffHoles      = 0x3C;
constexpr size_t kOffPredTex    = 0x40;   // 16-byte 2-bit-per-cell ground-effect layer map
constexpr size_t kOffNoEffectDoodad = 0x50;  // 8-byte 1-bit-per-cell doodad suppression
constexpr size_t kOffNSndEmitters= 0x58;  // count of MCSE sound emitters
constexpr size_t kOffOfsMCSE    = 0x5C;   // sound emitter sub-chunk offset
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

void setHoleBit(MapChunk& mc, int subX, int subY) {
    // Same bit layout cellIsHole() reads: sub-row (north-south) * 4 + sub-col.
    if (subX < 0 || subX > 3 || subY < 0 || subY > 3) return;
    mc.holes |= static_cast<uint16_t>(1u << (subY * 4 + subX));
}

void clearHoleBit(MapChunk& mc, int subX, int subY) {
    if (subX < 0 || subX > 3 || subY < 0 || subY > 3) return;
    mc.holes &= static_cast<uint16_t>(~(1u << (subY * 4 + subX)));
}

void setAllHoles(MapChunk& mc)   { mc.holes = 0xFFFF; }
void clearAllHoles(MapChunk& mc) { mc.holes = 0; }

void setChunkImpassable(MapChunk& mc, bool impassable) {
    if (impassable) mc.flags |= MCNK_IMPASSABLE;
    else            mc.flags &= ~MCNK_IMPASSABLE;
}

bool shadowAt(const MapChunk& mc, int row, int col) {
    if (mc.shadow.empty() || row < 0 || col < 0 || row >= 64 || col >= 64) return false;
    const size_t bit = static_cast<size_t>(row) * 64 + static_cast<size_t>(col);
    return ((mc.shadow[bit >> 3] >> (bit & 7)) & 1u) != 0;
}

int paintShadow(MapChunk& mc, float u, float v, float radius,
                float strength, Falloff falloff, bool set) {
    constexpr int    DIM    = 64;
    constexpr size_t kBytes = DIM * DIM / 8u;   // 512
    if (radius <= 0.0f || strength <= 0.0f) return 0;
    if (mc.shadow.empty() && !set) return 0;    // nothing to clear, don't allocate

    // 4x4 ordered-dither (Bayer) matrix. Per-texel thresholds (b + 0.5) / 16
    // spread uniformly over (0,1), so strength*weight in [0,1] sets a stable,
    // evenly dithered fraction of the covered texels; 1.0 clears every
    // threshold (solid), 0.5 sets half of them (checkered penumbra).
    static constexpr uint8_t kBayer4[4][4] = {
        {  0,  8,  2, 10 },
        { 12,  4, 14,  6 },
        {  3, 11,  1,  9 },
        { 15,  7, 13,  5 },
    };

    const float cx  = u * DIM;                  // brush centre in texel space
    const float cy  = v * DIM;
    const float rad = radius * DIM;
    int changed = 0;
    for (int row = 0; row < DIM; ++row) {
        for (int col = 0; col < DIM; ++col) {
            const float dx = (col + 0.5f) - cx;
            const float dy = (row + 0.5f) - cy;
            const float d  = std::sqrt(dx * dx + dy * dy);
            const float w  = falloffWeight(falloff, d, rad);
            if (w <= 0.0f) continue;
            const float threshold = (kBayer4[row & 3][col & 3] + 0.5f) / 16.0f;
            if (strength * w < threshold) continue;
            if (mc.shadow.size() < kBytes) mc.shadow.resize(kBytes, 0);  // first paint
            const size_t  bit  = static_cast<size_t>(row) * DIM + col;
            const uint8_t mask = static_cast<uint8_t>(1u << (bit & 7));
            uint8_t& b = mc.shadow[bit >> 3];
            const uint8_t before = b;
            if (set) b |= mask;
            else     b = static_cast<uint8_t>(b & ~mask);
            if (b != before) ++changed;
        }
    }
    return changed;
}

static MapChunk parseOneChunk(const uint8_t* data, uint32_t size) {
    if (size < kHdrSize)
        throw std::runtime_error("MCNK smaller than its 128-byte header");

    MapChunk mc;
    ByteReader h(data, kHdrSize);
    h.seek(kOffFlags);    mc.flags  = h.u32();

    // WoW 5.3+ repurposes the 8 bytes at +0x14 (vanilla ofsMCVT/ofsMCNR) as an
    // 8x8 high-res hole bitmap when this flag is set -- those "offsets" are
    // bitmap bits, not file positions, and dereferencing them would read
    // garbage. We don't support such ADTs; leave the chunk at defaults.
    if (mc.flags & MCNK_HIGH_RES_HOLES)
        return mc;

    h.seek(kOffIndexX);   mc.indexX = h.u32();
    h.seek(kOffIndexY);   mc.indexY = h.u32();
    h.seek(kOffAreaId);   mc.areaId = h.u32();
    h.seek(kOffHoles);    mc.holes  = h.u16();
    h.seek(kOffPredTex);
    for (uint8_t& b : mc.predTex)        b = h.u8();
    h.seek(kOffNoEffectDoodad);
    for (uint8_t& b : mc.noEffectDoodad) b = h.u8();
    h.seek(kOffPosition);
    mc.position = { h.f32(), h.f32(), h.f32() };

    auto u32At = [&](size_t off) -> uint32_t { ByteReader t(data, kHdrSize); t.seek(off); return t.u32(); };
    const uint32_t nLayers     = u32At(kOffNLayers);
    const uint32_t nDoodadRefs = u32At(kOffNDoodadRefs);
    const uint32_t nMapObjRefs = u32At(kOffNMapObjRefs);
    const uint32_t nSndEmitters= u32At(kOffNSndEmitters);
    const uint32_t ofsMCVT = u32At(kOffOfsMCVT);
    const uint32_t ofsMCNR = u32At(kOffOfsMCNR);
    const uint32_t ofsMCLY = u32At(kOffOfsMCLY);
    const uint32_t ofsMCRF = u32At(kOffOfsMCRF);
    const uint32_t ofsMCAL = u32At(kOffOfsMCAL);
    const uint32_t szMCAL  = u32At(kOffSizeMCAL);
    const uint32_t ofsMCSH = u32At(kOffOfsMCSH);
    const uint32_t ofsMCSE = u32At(kOffOfsMCSE);
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
            mc.mcvtOffset = static_cast<uint32_t>(p - data);   // chunk-relative; made absolute by parseChunks
            ByteReader r(p, sz);
            for (int i = 0; i < 145 && r.remaining() >= 4; ++i) mc.heights[i] = r.f32();
        }
        if (auto [p, sz] = sub(ofsMCNR, "MCNR"); p) {
            mc.mcnrOffset = static_cast<uint32_t>(p - data);   // chunk-relative; made absolute by parseChunks
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
        if (auto [p, sz] = sub(ofsMCRF, "MCRF"); p)
            parseMcrf(mc, p, sz, nDoodadRefs, nMapObjRefs);
        if (auto [p, sz] = sub(ofsMCSH, "MCSH"); p) {
            mc.mcshOffset = static_cast<uint32_t>(p - data);   // chunk-relative; made absolute by parseChunks
            parseMcsh(mc, p, sz);
        }
        if (auto [p, sz] = sub(ofsMCSE, "MCSE"); p)
            parseMcse(mc, p, sz, nSndEmitters);
        if (auto [p, sz] = sub(ofsMCLQ, "MCLQ"); p)
            parseMclq(mc, p, sz);

        return mc;
    }

    // Fallback: linear sub-chunk walk for files with no header offsets.
    const uint8_t* subData = data + kHdrSize;
    size_t subLen = size - kHdrSize;
    forEachChunk(subData, subLen, [&](const Chunk& c) {
        if (c.magic == "MCVT") {
            mc.mcvtOffset = static_cast<uint32_t>(c.data - data);   // chunk-relative; made absolute by parseChunks
            ByteReader r(c.data, c.size);
            for (int i = 0; i < 145 && r.remaining() >= 4; ++i) mc.heights[i] = r.f32();
        } else if (c.magic == "MCNR") {
            mc.mcnrOffset = static_cast<uint32_t>(c.data - data);   // chunk-relative; made absolute by parseChunks
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
        } else if (c.magic == "MCRF") {
            parseMcrf(mc, c.data, c.size, nDoodadRefs, nMapObjRefs);
        } else if (c.magic == "MCSH") {
            mc.mcshOffset = static_cast<uint32_t>(c.data - data);   // chunk-relative; made absolute by parseChunks
            parseMcsh(mc, c.data, c.size);
        } else if (c.magic == "MCSE") {
            parseMcse(mc, c.data, c.size, nSndEmitters);
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

void fixAlphaMapEdges(AlphaMap& map) {
    constexpr int D = AlphaMap::DIM;
    // Last column := previous column.
    for (int r = 0; r < D; ++r)
        map.texels[static_cast<size_t>(r) * D + (D - 1)] =
            map.texels[static_cast<size_t>(r) * D + (D - 2)];
    // Last row := previous row (done after the column pass, so the bottom-right
    // corner inherits texel (62,62) -- matching the client's fixed-up map).
    for (int c = 0; c < D; ++c)
        map.texels[static_cast<size_t>(D - 1) * D + c] =
            map.texels[static_cast<size_t>(D - 2) * D + c];
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

// Parse MCRF: nDoodad uint32 indices into the ADT's MDDF list, then nWmo indices
// into its MODF list (the doodads/objects placed within this chunk's footprint).
static void parseMcrf(MapChunk& mc, const uint8_t* data, uint32_t size,
                      uint32_t nDoodad, uint32_t nWmo) {
    ByteReader r(data, size);
    for (uint32_t i = 0; i < nDoodad && r.remaining() >= 4; ++i) mc.doodadRefs.push_back(r.u32());
    for (uint32_t i = 0; i < nWmo    && r.remaining() >= 4; ++i) mc.wmoRefs.push_back(r.u32());
}

// Parse MCSH: a 64x64-bit (512-byte) baked shadow bitmap. Truncated maps are
// zero-padded so shadowAt() can index any texel safely.
//
// Like MCAL, the last row/column carries no real data, and the client
// duplicates the previous one unless MCNK_DO_NOT_FIX_ALPHA is set (the flag
// governs both maps). Unlike the alpha path -- where the fix is deferred to
// draw time so decode/encode stays a loss-less codec -- the fix is applied
// here at parse time and shadowAt() / the renderer / paintShadow() see fixed
// data directly. That makes writeAdtShadows() write the FIXED edge bits back,
// which is deliberate and harmless: the edge carries no real data and the
// client re-duplicates it on load either way (see writeAdtShadows).
static void parseMcsh(MapChunk& mc, const uint8_t* data, uint32_t size) {
    constexpr uint32_t kBytes = 64u * 64u / 8u;   // 512
    uint32_t n = std::min(size, kBytes);
    mc.shadow.assign(data, data + n);
    if (mc.shadow.size() < kBytes) mc.shadow.resize(kBytes, 0);

    if (mc.flags & MCNK_DO_NOT_FIX_ALPHA) return;

    // 63->64 duplication at the bit level (rows are 8 bytes, LSB-first).
    // Column first: bit 63 of each row := bit 62 (both live in the row's last
    // byte, at bit positions 7 and 6). Then row 63 := row 62 wholesale, so the
    // corner inherits texel (62,62) -- same order as fixAlphaMapEdges.
    for (int row = 0; row < 64; ++row) {
        uint8_t& last = mc.shadow[static_cast<size_t>(row) * 8 + 7];
        const uint8_t bit62 = (last >> 6) & 1u;
        last = static_cast<uint8_t>((last & 0x7F) | (bit62 << 7));
    }
    std::copy(mc.shadow.begin() + 62 * 8, mc.shadow.begin() + 63 * 8,
              mc.shadow.begin() + 63 * 8);
}

// Parse MCSE: nSndEmitters 52-byte 1.12.1 sound-emitter records --
//   uint32 soundPointID, uint32 soundNameID, float pos[3] (at +0x08),
//   float minDistance, maxDistance, cutoffDistance,
//   uint16 startTime, endTime, mode, uint8 loopCountMin, loopCountMax,
//   uint16 groupSilenceMin/Max, playInstancesMin/Max, interSoundGapMin/Max.
// (The 28-byte SoundEmitterRec with the position at +0x04 is the TBC+ layout;
// vanilla files use this fatter record.) The ids, position and the three
// distances are kept; the 20-byte timing/count tail is skipped to hold the
// 52-byte stride. The count is clamped to what the chunk actually holds, so a
// short MCSE stops cleanly rather than reading a partial record.
static void parseMcse(MapChunk& mc, const uint8_t* data, uint32_t size, uint32_t nSndEmitters) {
    constexpr uint32_t kStride = 52u;
    ByteReader r(data, size);
    for (uint32_t i = 0; i < nSndEmitters && r.remaining() >= kStride; ++i) {
        SoundEmitter e;
        e.soundPointID   = r.u32();
        e.soundNameID    = r.u32();
        e.soundId        = e.soundPointID;   // legacy alias
        e.position       = { r.f32(), r.f32(), r.f32() };
        e.minDistance    = r.f32();
        e.maxDistance    = r.f32();
        e.cutoffDistance = r.f32();
        r.skip(kStride - 32u);   // skip the timing/count tail
        mc.soundEmitters.push_back(e);
    }
}

// Parse the MCLQ liquid layers. The chunk's declared size is unreliable in
// real vanilla files, so the layout is driven by the MCNK header instead: one
// 804-byte block per LQ flag set, in fixed river/ocean/magma/slime order.
// Each block is CRange min/max (8 B) + 81 vertices x 8 B (a 4-byte per-type
// union, then the height float) + 64 tile-flag bytes + uint32 nFlowvs + two
// 40-byte SWFlowv records that are ALWAYS on disk regardless of nFlowvs
// (8 + 648 + 64 + 4 + 80 = 804). Truncated data stops the walk, keeping the
// layers already parsed. The first layer is mirrored into hasLiquid /
// liquidType / liquid for the existing single-layer consumers.
static void parseMclq(MapChunk& mc, const uint8_t* data, uint32_t size) {
    constexpr size_t kCoreBytes = 8u + 81u * 8u + 64u;   // through the tile flags
    constexpr size_t kFlowBytes = 4u + 2u * 40u;         // nFlowvs + 2 fixed SWFlowv

    static constexpr struct { uint32_t flag; LiquidType type; } kOrder[] = {
        { MCNK_LQ_RIVER, LiquidType::River },
        { MCNK_LQ_OCEAN, LiquidType::Ocean },
        { MCNK_LQ_MAGMA, LiquidType::Magma },
        { MCNK_LQ_SLIME, LiquidType::Slime },
    };

    ByteReader r(data, size);
    for (const auto& o : kOrder) {
        if (!(mc.flags & o.flag)) continue;
        if (r.remaining() < kCoreBytes) break;   // truncated: keep parsed layers

        const bool isWater = (o.type == LiquidType::River || o.type == LiquidType::Ocean);

        MclqLayer L;
        L.type      = o.type;
        L.minHeight = r.f32();
        L.maxHeight = r.f32();
        for (int i = 0; i < 81; ++i) {
            // The 4-byte union is water {depth,flow0Pct,flow1Pct,filler}, ocean
            // {depth,foam,wet,filler} or magma/slime {uint16 s, uint16 t}. For
            // water/ocean the first byte is the depth (0..255) that drives
            // shoreline transparency; magma texcoords we don't need here.
            uint8_t b0 = r.u8();
            r.skip(3);                // remaining union bytes (flow / texcoord tail)
            L.depth[i]   = isWater ? b0 : 0;
            L.heights[i] = r.f32();   // height is always the last 4 bytes
        }
        for (int i = 0; i < 64; ++i) L.renderFlags[i] = r.u8();
        mc.liquidLayers.push_back(L);

        // Flow tail: without it the next layer can't be located, so a short
        // tail ends the walk (this layer is already kept).
        if (r.remaining() < kFlowBytes) break;
        r.skip(kFlowBytes);
    }

    if (mc.liquidLayers.empty()) return;
    mc.hasLiquid  = true;
    mc.liquidType = mc.liquidLayers.front().type;
    mc.liquid     = mc.liquidLayers.front();
}

std::vector<MapChunk> parseChunks(const std::vector<uint8_t>& adtBuf) {
    std::vector<MapChunk> out;
    out.reserve(256);
    forEachChunk(adtBuf.data(), adtBuf.size(), [&](const Chunk& c) {
        if (c.magic == "MCNK") {
            MapChunk mc = parseOneChunk(c.data, c.size);
            // parseOneChunk recorded the sub-chunk offsets relative to the chunk's
            // data start; lift them to absolute offsets into adtBuf so the writers
            // can patch in place.
            const uint32_t base = static_cast<uint32_t>(c.data - adtBuf.data());
            if (mc.mcvtOffset) mc.mcvtOffset += base;
            if (mc.mcnrOffset) mc.mcnrOffset += base;
            if (mc.mcshOffset) mc.mcshOffset += base;
            // The 128-byte header sits at the chunk data start. Left 0 for
            // unsupported high-res-hole chunks so the header patchers skip them
            // (their parsed fields are defaults, not the file's values).
            if (!(mc.flags & MCNK_HIGH_RES_HOLES)) mc.mcnkHeaderOffset = base;
            out.push_back(std::move(mc));
        }
        return true;
    });
    return out;
}

std::vector<uint8_t> writeAdtHeights(const std::vector<uint8_t>& adtBuf,
                                     const std::vector<MapChunk>& chunks) {
    std::vector<uint8_t> out = adtBuf;   // start byte-identical to the original
    for (const MapChunk& mc : chunks) {
        if (mc.mcvtOffset == 0) continue;            // no in-file MCVT to patch
        const size_t base = mc.mcvtOffset;
        if (base + 145u * 4u > out.size())
            throw std::runtime_error("writeAdtHeights: MCVT offset past end of ADT "
                                     "(chunks do not match this buffer)");
        for (int i = 0; i < 145; ++i) {
            uint32_t bits;
            std::memcpy(&bits, &mc.heights[i], sizeof(bits));
            for (int b = 0; b < 4; ++b)              // little-endian, like ByteReader::f32
                out[base + static_cast<size_t>(i) * 4 + b] =
                    static_cast<uint8_t>((bits >> (8 * b)) & 0xFF);
        }
    }
    return out;
}

std::vector<uint8_t> writeAdtNormals(const std::vector<uint8_t>& adtBuf,
                                     const std::vector<MapChunk>& chunks) {
    std::vector<uint8_t> out = adtBuf;
    auto toI8 = [](float c) -> uint8_t {
        long q = std::lround(std::clamp(c, -1.0f, 1.0f) * 127.0f);
        return static_cast<uint8_t>(static_cast<int8_t>(std::clamp(q, -127L, 127L)));
    };
    for (const MapChunk& mc : chunks) {
        if (mc.mcnrOffset == 0) continue;
        const size_t base = mc.mcnrOffset;
        if (base + 145u * 3u > out.size())
            throw std::runtime_error("writeAdtNormals: MCNR offset past end of ADT "
                                     "(chunks do not match this buffer)");
        for (int i = 0; i < 145; ++i) {
            const Vec3& n = mc.normals[i];
            out[base + static_cast<size_t>(i) * 3 + 0] = toI8(n.x);   // file order x,y,z
            out[base + static_cast<size_t>(i) * 3 + 1] = toI8(n.y);
            out[base + static_cast<size_t>(i) * 3 + 2] = toI8(n.z);
        }
    }
    return out;
}

std::vector<uint8_t> writeAdtShadows(const std::vector<uint8_t>& adtBuf,
                                     const std::vector<MapChunk>& chunks,
                                     int* skippedNoMcsh) {
    constexpr size_t kBytes = 64u * 64u / 8u;   // 512
    if (skippedNoMcsh) *skippedNoMcsh = 0;
    std::vector<uint8_t> out = adtBuf;   // start byte-identical to the original
    for (const MapChunk& mc : chunks) {
        if (mc.mcshOffset == 0) {
            // No in-file MCSH to patch. A shadow map painted onto such a chunk
            // needs whole-chunk re-serialisation (see the header note): count
            // it so the caller knows the edit did NOT reach the file.
            if (!mc.shadow.empty() && skippedNoMcsh) ++*skippedNoMcsh;
            continue;
        }
        if (mc.shadow.empty()) continue;         // parsed map discarded: keep file bytes
        const size_t base = mc.mcshOffset;
        if (base + kBytes > out.size())
            throw std::runtime_error("writeAdtShadows: MCSH offset past end of ADT "
                                     "(chunks do not match this buffer)");
        const size_t n = std::min(mc.shadow.size(), kBytes);
        std::copy(mc.shadow.begin(), mc.shadow.begin() + static_cast<long>(n),
                  out.begin() + static_cast<long>(base));
    }
    return out;
}

// Shared plumbing for the MCNK-header patchers: copy adtBuf, then let `poke`
// write into each chunk's 128-byte header (located via mcnkHeaderOffset;
// chunks with offset 0 -- not parsed from a buffer, or unsupported -- are
// skipped). Bounds are checked once here so every patcher inherits the same
// "chunks must match this buffer" guarantee writeAdtHeights gives.
namespace {
template <class Poke>
std::vector<uint8_t> patchMcnkHeaders(const std::vector<uint8_t>& adtBuf,
                                      const std::vector<MapChunk>& chunks,
                                      const char* who, Poke&& poke) {
    std::vector<uint8_t> out = adtBuf;   // start byte-identical to the original
    for (const MapChunk& mc : chunks) {
        if (mc.mcnkHeaderOffset == 0) continue;          // no in-file header to patch
        const size_t base = mc.mcnkHeaderOffset;
        if (base + kHdrSize > out.size())
            throw std::runtime_error(std::string(who) +
                                     ": MCNK header offset past end of ADT "
                                     "(chunks do not match this buffer)");
        poke(out, mc, base);
    }
    return out;
}

void pokeU16(std::vector<uint8_t>& buf, size_t off, uint16_t v) {
    buf[off]     = static_cast<uint8_t>(v & 0xFF);       // little-endian, like ByteReader::u16
    buf[off + 1] = static_cast<uint8_t>((v >> 8) & 0xFF);
}
void pokeU32(std::vector<uint8_t>& buf, size_t off, uint32_t v) {
    for (int b = 0; b < 4; ++b)
        buf[off + static_cast<size_t>(b)] = static_cast<uint8_t>((v >> (8 * b)) & 0xFF);
}
} // namespace

std::vector<uint8_t> writeAdtHoles(const std::vector<uint8_t>& adtBuf,
                                   const std::vector<MapChunk>& chunks) {
    return patchMcnkHeaders(adtBuf, chunks, "writeAdtHoles",
        [](std::vector<uint8_t>& out, const MapChunk& mc, size_t base) {
            pokeU16(out, base + kOffHoles, mc.holes);
        });
}

std::vector<uint8_t> writeAdtAreaIds(const std::vector<uint8_t>& adtBuf,
                                     const std::vector<MapChunk>& chunks) {
    return patchMcnkHeaders(adtBuf, chunks, "writeAdtAreaIds",
        [](std::vector<uint8_t>& out, const MapChunk& mc, size_t base) {
            pokeU32(out, base + kOffAreaId, mc.areaId);
        });
}

std::vector<uint8_t> writeAdtChunkFlags(const std::vector<uint8_t>& adtBuf,
                                        const std::vector<MapChunk>& chunks) {
    return patchMcnkHeaders(adtBuf, chunks, "writeAdtChunkFlags",
        [](std::vector<uint8_t>& out, const MapChunk& mc, size_t base) {
            pokeU32(out, base + kOffFlags, mc.flags);
        });
}

std::vector<uint8_t> writeAdtPredTex(const std::vector<uint8_t>& adtBuf,
                                     const std::vector<MapChunk>& chunks) {
    return patchMcnkHeaders(adtBuf, chunks, "writeAdtPredTex",
        [](std::vector<uint8_t>& out, const MapChunk& mc, size_t base) {
            for (size_t i = 0; i < mc.predTex.size(); ++i)
                out[base + kOffPredTex + i] = mc.predTex[i];
            for (size_t i = 0; i < mc.noEffectDoodad.size(); ++i)
                out[base + kOffNoEffectDoodad + i] = mc.noEffectDoodad[i];
        });
}

std::array<uint8_t, 64> decodePredTex(const uint8_t* packed) {
    std::array<uint8_t, 64> cells{};
    for (int k = 0; k < 64; ++k)
        cells[k] = (packed[k / 4] >> ((k % 4) * 2)) & 0x3;
    return cells;
}

std::array<uint8_t, 16> encodePredTex(const std::array<uint8_t, 64>& cells) {
    std::array<uint8_t, 16> packed{};
    for (int k = 0; k < 64; ++k)
        packed[k / 4] |= static_cast<uint8_t>((cells[k] & 0x3) << ((k % 4) * 2));
    return packed;
}

std::array<uint8_t, 64> computePredominantLayer(const MapChunk& mc, bool bigAlpha) {
    std::array<uint8_t, 64> cells{};
    const size_t nLayers = std::min<size_t>(mc.layers.size(), 4);   // vanilla layer cap
    if (nLayers <= 1) return cells;   // base coat only (no alphas): every cell 0

    std::array<AlphaMap, 4> maps;     // maps[1..3]; [0] unused (base has no alpha)
    for (size_t i = 1; i < nLayers; ++i)
        maps[i] = decodeAlphaMap(mc, i, bigAlpha);

    for (int r = 0; r < 8; ++r) {
        for (int c = 0; c < 8; ++c) {
            // Sample every layer at the subcell's centre texel.
            const int row = r * 8 + 4, col = c * 8 + 4;
            // Sequential-lerp visibility, walked top layer down: a layer's
            // effective weight is its own alpha times the transparency of
            // every layer above it; whatever filters through them all is what
            // remains of the base coat.
            float eff[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
            float vis = 1.0f;   // prod of (1 - a_j) over the layers above
            for (int i = static_cast<int>(nLayers) - 1; i >= 1; --i) {
                const float a = maps[i].at(row, col) / 255.0f;
                eff[i] = a * vis;
                vis *= 1.0f - a;
            }
            eff[0] = vis;
            int best = 0;                          // ties go to the lower layer
            for (int i = 1; i < static_cast<int>(nLayers); ++i)
                if (eff[i] > eff[best]) best = i;
            cells[r * 8 + c] = static_cast<uint8_t>(best);   // 2-bit value, 0..3
        }
    }
    return cells;
}

void updatePredTex(MapChunk& mc, bool bigAlpha) {
    mc.predTex = encodePredTex(computePredominantLayer(mc, bigAlpha));
}

void setNoEffectDoodad(MapChunk& mc, int subX, int subY, bool suppress) {
    if (subX < 0 || subX > 7 || subY < 0 || subY > 7) return;
    const uint8_t mask = static_cast<uint8_t>(1u << subX);   // row-major, LSB-first
    if (suppress) mc.noEffectDoodad[subY] |= mask;
    else          mc.noEffectDoodad[subY] = static_cast<uint8_t>(mc.noEffectDoodad[subY] & ~mask);
}

void recomputeTileNormals(std::vector<MapChunk>& chunks) {
    // Locate chunks by their grid position (row = IndexY, col = IndexX).
    int grid[16][16];
    for (auto& r : grid) for (int& v : r) v = -1;
    for (size_t c = 0; c < chunks.size(); ++c) {
        const int r = static_cast<int>(chunks[c].indexY);
        const int co = static_cast<int>(chunks[c].indexX);
        if (r >= 0 && r < 16 && co >= 0 && co < 16) grid[r][co] = static_cast<int>(c);
    }
    const float U = static_cast<float>(UNIT_SIZE);

    // Absolute surface height at a global outer-grid node (gr,gc) in [0,128].
    // Multiples of 8 land on a shared chunk edge; either neighbour stores the
    // same height there, so capping the chunk index is safe. Returns false off
    // the tile or where the covering chunk is absent.
    auto sampleH = [&](int gr, int gc, float& out) -> bool {
        if (gr < 0 || gr > 128 || gc < 0 || gc > 128) return false;
        const int cr = std::min(gr / 8, 15), i = gr - cr * 8;
        const int cc = std::min(gc / 8, 15), j = gc - cc * 8;
        const int ci = grid[cr][cc];
        if (ci < 0) return false;
        out = chunks[ci].position.z + chunks[ci].heights[i * 17 + j];
        return true;
    };
    // Heightfield normal at global node (gr,gc): for z=f(worldX,worldY) the normal
    // is normalize(-df/dx, -df/dy, 1). gr advances south (-worldX), gc advances
    // east (-worldY), so the signs fold into the central differences below.
    auto normalAt = [&](int gr, int gc) -> Vec3 {
        float h = 0, hN = 0, hS = 0, hW = 0, hE = 0;
        sampleH(gr, gc, h);
        const bool okN = sampleH(gr - 1, gc, hN), okS = sampleH(gr + 1, gc, hS);
        const bool okW = sampleH(gr, gc - 1, hW), okE = sampleH(gr, gc + 1, hE);
        if (!okN) hN = h;  if (!okS) hS = h;
        if (!okW) hW = h;  if (!okE) hE = h;
        int spanR = (okN ? 1 : 0) + (okS ? 1 : 0); if (spanR == 0) spanR = 1;
        int spanC = (okW ? 1 : 0) + (okE ? 1 : 0); if (spanC == 0) spanC = 1;
        const float nx = (hS - hN) / (spanR * U);
        const float ny = (hE - hW) / (spanC * U);
        return normalize(Vec3{ nx, ny, 1.0f });
    };

    for (MapChunk& mc : chunks) {
        const int row = static_cast<int>(mc.indexY);
        const int col = static_cast<int>(mc.indexX);
        // Outer 9x9 normals, straight from the shared global grid (seamless).
        for (int i = 0; i < 9; ++i)
            for (int j = 0; j < 9; ++j)
                mc.normals[i * 17 + j] = normalAt(row * 8 + i, col * 8 + j);
        // Inner 8x8: mean of the four surrounding outer normals.
        for (int i = 0; i < 8; ++i)
            for (int j = 0; j < 8; ++j) {
                Vec3 n = normalAt(row * 8 + i,     col * 8 + j)
                       + normalAt(row * 8 + i,     col * 8 + j + 1)
                       + normalAt(row * 8 + i + 1, col * 8 + j)
                       + normalAt(row * 8 + i + 1, col * 8 + j + 1);
                mc.normals[i * 17 + 9 + j] = normalize(n);
            }
    }
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
