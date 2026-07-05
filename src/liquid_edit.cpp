#include "liquid_edit.hpp"

#include <algorithm>
#include <cmath>

#include "byte_writer.hpp"
#include "coords.hpp"
#include "editing.hpp"   // FlattenPlane / flattenPlaneTarget (same plane maths as terrain)

namespace wf {

namespace {

// LQ-flag serialization order: river, ocean, magma, slime. The same walk
// parseMclq (terrain.cpp) drives its reads with -- keep the two in lockstep.
constexpr LiquidType kLqOrder[] = {
    LiquidType::River, LiquidType::Ocean, LiquidType::Magma, LiquidType::Slime,
};

bool isWaterType(LiquidType type) {
    return type == LiquidType::River || type == LiquidType::Ocean;
}

MclqLayer* findLayer(MapChunk& mc, LiquidType type) {
    for (MclqLayer& L : mc.liquidLayers)
        if (L.type == type) return &L;
    return nullptr;
}

const MclqLayer* findLayer(const MapChunk& mc, LiquidType type) {
    for (const MclqLayer& L : mc.liquidLayers)
        if (L.type == type) return &L;
    return nullptr;
}

// Refresh the single-layer back-compat mirror (hasLiquid / liquidType /
// liquid) from the first remaining layer -- same rule as the parser, so the
// existing render path (buildLiquidMesh reads mc.liquid) tracks edits.
void syncLiquidMirror(MapChunk& mc) {
    if (mc.liquidLayers.empty()) {
        mc.hasLiquid  = false;
        mc.liquidType = LiquidType::None;
        mc.liquid     = MclqLayer{};
        return;
    }
    mc.hasLiquid  = true;
    mc.liquidType = mc.liquidLayers.front().type;
    mc.liquid     = mc.liquidLayers.front();
}

// True when every one of the layer's 64 cells is masked "don't render".
bool layerEmpty(const MclqLayer& L) {
    for (uint8_t f : L.renderFlags)
        if (liquidTileRenders(f)) return false;
    return true;
}

// Drop the chunk's `type` layer, clear its MCNK LQ flag and re-sync the
// mirror (promoting the next layer, if any).
void dropLayer(MapChunk& mc, LiquidType type) {
    mc.liquidLayers.erase(
        std::remove_if(mc.liquidLayers.begin(), mc.liquidLayers.end(),
                       [type](const MclqLayer& L) { return L.type == type; }),
        mc.liquidLayers.end());
    mc.flags &= ~liquidTypeFlag(type);
    syncLiquidMirror(mc);
}

// Absolute terrain height under liquid vertex (r,c): the paired 9x9 OUTER
// MCVT vertex. The interleaved 145-float grid stores outer row r at stride
// 17, so outer (r,c) = heights[r*17 + c]; MCVT is relative to position.z.
float terrainHeightAt(const MapChunk& mc, int r, int c) {
    return mc.position.z + mc.heights[static_cast<size_t>(r) * 17 + static_cast<size_t>(c)];
}

} // namespace

uint32_t liquidTypeFlag(LiquidType type) {
    switch (type) {
        case LiquidType::River: return MCNK_LQ_RIVER;
        case LiquidType::Ocean: return MCNK_LQ_OCEAN;
        case LiquidType::Magma: return MCNK_LQ_MAGMA;
        case LiquidType::Slime: return MCNK_LQ_SLIME;
        case LiquidType::None:  break;
    }
    return 0;
}

uint8_t liquidTypeNibble(LiquidType type) {
    switch (type) {
        case LiquidType::Ocean: return 0x1;
        case LiquidType::Slime: return 0x3;
        case LiquidType::River: return 0x4;
        case LiquidType::Magma: return 0x6;
        case LiquidType::None:  break;
    }
    return MCLQ_TILE_HIDDEN;
}

MclqLayer& ensureLiquidLayer(MapChunk& mc, LiquidType type) {
    if (type == LiquidType::None) return mc.liquid;   // documented no-op guard
    if (MclqLayer* L = findLayer(mc, type)) return *L;

    MclqLayer L;
    L.type = type;
    L.renderFlags.fill(MCLQ_TILE_HIDDEN);   // new layer starts fully masked off

    // Insert keeping liquidLayers in LQ-flag order (the serializer's and the
    // parser's block order): before the first layer with a larger flag.
    const uint32_t flag = liquidTypeFlag(type);
    auto pos = mc.liquidLayers.begin();
    while (pos != mc.liquidLayers.end() && liquidTypeFlag(pos->type) < flag) ++pos;
    pos = mc.liquidLayers.insert(pos, L);

    mc.flags |= flag;
    syncLiquidMirror(mc);
    return *pos;
}

void setLiquidCells(MapChunk& mc, LiquidType type, uint64_t cellMask8x8, float height) {
    if (type == LiquidType::None) return;
    MclqLayer& L = ensureLiquidLayer(mc, type);

    const uint8_t nibble = liquidTypeNibble(type);
    for (int r = 0; r < 8; ++r) {
        for (int c = 0; c < 8; ++c) {
            const int bit = r * 8 + c;
            if (!((cellMask8x8 >> bit) & 1u)) continue;
            // Write the type nibble, preserving the behaviour high nibble.
            L.renderFlags[bit] = static_cast<uint8_t>((L.renderFlags[bit] & 0xF0) | nibble);
            // Cell (r,c) is covered by the four vertices (r..r+1, c..c+1).
            for (int i = r; i <= r + 1; ++i)
                for (int j = c; j <= c + 1; ++j)
                    L.heights[static_cast<size_t>(i) * 9 + static_cast<size_t>(j)] = height;
        }
    }
    recomputeMinMax(L);
    syncLiquidMirror(mc);
}

void clearLiquidCells(MapChunk& mc, LiquidType type, uint64_t cellMask8x8) {
    MclqLayer* L = findLayer(mc, type);
    if (!L) return;

    for (int bit = 0; bit < 64; ++bit)
        if ((cellMask8x8 >> bit) & 1u)
            L->renderFlags[static_cast<size_t>(bit)] = MCLQ_TILE_HIDDEN;

    if (layerEmpty(*L)) {
        dropLayer(mc, type);   // also clears the LQ flag + promotes the mirror
        return;
    }
    recomputeMinMax(*L);
    syncLiquidMirror(mc);
}

int fillTileWater(std::vector<MapChunk>& chunks, LiquidType type, float height) {
    if (type == LiquidType::None) return 0;
    int enabled = 0;
    for (MapChunk& mc : chunks) {
        if (const MclqLayer* L = findLayer(mc, type)) {
            for (uint8_t f : L->renderFlags)
                if (!liquidTileRenders(f)) ++enabled;
        } else {
            enabled += 64;   // no layer yet: every cell is newly enabled
        }
        setLiquidCells(mc, type, ~0ull, height);
    }
    return enabled;
}

int clearTileLiquid(std::vector<MapChunk>& chunks, LiquidType type) {
    int wiped = 0;
    for (MapChunk& mc : chunks) {
        if (!findLayer(mc, type)) continue;
        clearLiquidCells(mc, type, ~0ull);
        ++wiped;
    }
    return wiped;
}

void tiltLiquid(MapChunk& mc, LiquidType type, Vec3 lock,
                float orientationDeg, float angleDeg) {
    MclqLayer* L = findLayer(mc, type);
    if (!L) return;   // tilt on an absent layer is a no-op

    // Same plane maths as the terrain flatten tool. flattenPlaneTarget wraps
    // the orientation to [0,360) and clamps the angle to [0,89].
    const FlattenPlane plane{ lock, orientationDeg, angleDeg };

    // Liquid vertex (i,j) shares the terrain outer ring's world XY: the MCNK
    // header position is the chunk's NW corner (verified in coords.hpp), with
    // rows advancing south (-X) and columns east (-Y) at UNIT_SIZE spacing.
    const float U = static_cast<float>(UNIT_SIZE);
    for (int i = 0; i < 9; ++i) {
        for (int j = 0; j < 9; ++j) {
            const float wx = mc.position.x - static_cast<float>(i) * U;
            const float wy = mc.position.y - static_cast<float>(j) * U;
            L->heights[static_cast<size_t>(i) * 9 + static_cast<size_t>(j)] =
                flattenPlaneTarget(plane, wx, wy);
        }
    }
    recomputeMinMax(*L);
    syncLiquidMirror(mc);
}

int cropBelowTerrain(MapChunk& mc, LiquidType type) {
    MclqLayer* L = findLayer(mc, type);
    if (!L) return 0;

    int removed = 0;
    for (int r = 0; r < 8; ++r) {
        for (int c = 0; c < 8; ++c) {
            const int bit = r * 8 + c;
            if (!liquidTileRenders(L->renderFlags[bit])) continue;
            // Remove only when the liquid is strictly under the ground at ALL
            // four corners; a shoreline touch (liquid == terrain) keeps the cell.
            bool allBelow = true;
            for (int i = r; i <= r + 1 && allBelow; ++i)
                for (int j = c; j <= c + 1 && allBelow; ++j) {
                    const float liquidZ =
                        L->heights[static_cast<size_t>(i) * 9 + static_cast<size_t>(j)];
                    if (liquidZ >= terrainHeightAt(mc, i, j)) allBelow = false;
                }
            if (!allBelow) continue;
            L->renderFlags[bit] = MCLQ_TILE_HIDDEN;
            ++removed;
        }
    }

    if (layerEmpty(*L)) {
        dropLayer(mc, type);
        return removed;
    }
    if (removed) {
        recomputeMinMax(*L);
        syncLiquidMirror(mc);
    }
    return removed;
}

void autoDepth(MapChunk& mc, LiquidType type, float factor) {
    if (!isWaterType(type)) return;   // magma/slime carry texcoords, not depth
    MclqLayer* L = findLayer(mc, type);
    if (!L) return;

    for (int i = 0; i < 9; ++i) {
        for (int j = 0; j < 9; ++j) {
            const size_t v = static_cast<size_t>(i) * 9 + static_cast<size_t>(j);
            const float column = L->heights[v] - terrainHeightAt(mc, i, j);
            const long  d      = std::lround(static_cast<double>(column) * factor);
            L->depth[v] = static_cast<uint8_t>(std::clamp(d, 0L, 255L));
        }
    }
    syncLiquidMirror(mc);
}

void recomputeMinMax(MclqLayer& layer) {
    bool  any  = false;
    float minH = 0.0f, maxH = 0.0f;
    for (int r = 0; r < 8; ++r) {
        for (int c = 0; c < 8; ++c) {
            if (!liquidTileRenders(layer.renderFlags[r * 8 + c])) continue;
            for (int i = r; i <= r + 1; ++i)
                for (int j = c; j <= c + 1; ++j) {
                    const float h =
                        layer.heights[static_cast<size_t>(i) * 9 + static_cast<size_t>(j)];
                    if (!any) { minH = maxH = h; any = true; }
                    else {
                        minH = std::min(minH, h);
                        maxH = std::max(maxH, h);
                    }
                }
        }
    }
    layer.minHeight = minH;   // 0/0 when fully masked
    layer.maxHeight = maxH;
}

std::vector<uint8_t> encodeMclq(const MapChunk& mc) {
    ByteWriter w;
    for (LiquidType type : kLqOrder) {
        const MclqLayer* L = findLayer(mc, type);
        if (!L) continue;

        w.f32(L->minHeight);
        w.f32(L->maxHeight);
        for (int i = 0; i < 9; ++i) {
            for (int j = 0; j < 9; ++j) {
                const size_t v = static_cast<size_t>(i) * 9 + static_cast<size_t>(j);
                if (isWaterType(type)) {
                    w.u8(L->depth[v]);   // shoreline-transparency depth byte
                    w.u8(0);             // flow0Pct (flows not authored)
                    w.u8(0);             // flow1Pct
                    w.u8(0);             // filler
                } else {
                    // Magma/slime texcoords, regenerated (not retained by the
                    // parser): with the 1.12 client's 3.0/256.0 ADT UV scale,
                    // col*32 spans uv 0..3 across the chunk -- the texture
                    // tiles three times per chunk edge.
                    w.u16(static_cast<uint16_t>(j * 32));   // s
                    w.u16(static_cast<uint16_t>(i * 32));   // t
                }
                w.f32(L->heights[v]);
            }
        }
        for (uint8_t f : L->renderFlags) w.u8(f);
        w.u32(0);                                        // nFlowvs
        for (int i = 0; i < 2 * 40; ++i) w.u8(0);        // two fixed SWFlowv records
    }
    return w.take();
}

uint32_t mcnkLiquidFlags(const MapChunk& mc) {
    uint32_t flags = 0;
    for (const MclqLayer& L : mc.liquidLayers) flags |= liquidTypeFlag(L.type);
    return flags;
}

} // namespace wf
