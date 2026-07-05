#include "test.hpp"
#include "liquid_edit.hpp"
#include "terrain.hpp"
#include "coords.hpp"

#include <cmath>
#include <cstring>
#include <vector>

using namespace wf;

namespace {
// Little-endian readers for walking the encoded MCLQ blob.
uint16_t rd16(const std::vector<uint8_t>& b, size_t off) {
    return static_cast<uint16_t>(b[off] | (b[off + 1] << 8));
}
uint32_t rd32(const std::vector<uint8_t>& b, size_t off) {
    uint32_t v = 0;
    for (int i = 3; i >= 0; --i) v = (v << 8) | b[off + static_cast<size_t>(i)];
    return v;
}
float rdf(const std::vector<uint8_t>& b, size_t off) {
    uint32_t v = rd32(b, off);
    float f;
    std::memcpy(&f, &v, 4);
    return f;
}

// Synthetic-MCNK builders for the parse round-trip (same wire helpers as
// test_terrain: reversed magic + u32 size + payload).
void put32(std::vector<uint8_t>& b, uint32_t v) {
    for (int i = 0; i < 4; i++) b.push_back(static_cast<uint8_t>((v >> (8 * i)) & 0xFF));
}
void chunk(std::vector<uint8_t>& b, const char* m, const std::vector<uint8_t>& p) {
    b.push_back(static_cast<uint8_t>(m[3]));
    b.push_back(static_cast<uint8_t>(m[2]));
    b.push_back(static_cast<uint8_t>(m[1]));
    b.push_back(static_cast<uint8_t>(m[0]));
    put32(b, static_cast<uint32_t>(p.size()));
    b.insert(b.end(), p.begin(), p.end());
}
} // namespace

void test_liquid_edit() {
    std::printf("[liquid_edit]\n");

    // --- ensure + setLiquidCells: tile nibbles, covered heights, flags ------
    MapChunk mc;
    mc.position = {0, 0, 0};
    setLiquidCells(mc, LiquidType::River, 0x3ull /*cells (0,0),(0,1)*/, 12.0f);
    CHECK(mc.flags & MCNK_LQ_RIVER);
    CHECK(mc.hasLiquid);
    CHECK(mc.liquidType == LiquidType::River);
    CHECK(mc.liquidLayers.size() == 1);
    {
        const MclqLayer& L = mc.liquidLayers[0];
        CHECK(L.renderFlags[0] == 0x04);          // river nibble
        CHECK(L.renderFlags[1] == 0x04);
        CHECK(L.renderFlags[2] == 0x0F);          // untouched cell stays hidden
        CHECK(L.renderFlags[63] == 0x0F);
        // Cells (0,0)+(0,1) cover vertices rows 0..1, cols 0..2 of the 9x9 grid.
        CHECK_APPROX(L.heights[0 * 9 + 0], 12.0f);
        CHECK_APPROX(L.heights[0 * 9 + 2], 12.0f);
        CHECK_APPROX(L.heights[1 * 9 + 2], 12.0f);
        CHECK_APPROX(L.heights[0 * 9 + 3], 0.0f);  // just outside the coverage
        CHECK_APPROX(L.heights[2 * 9 + 0], 0.0f);
        CHECK_APPROX(L.minHeight, 12.0f);
        CHECK_APPROX(L.maxHeight, 12.0f);
        // Back-compat mirror tracks the first layer.
        CHECK(mc.liquid.renderFlags[0] == 0x04);
        CHECK_APPROX(mc.liquid.heights[0], 12.0f);
    }

    // Behaviour high nibble (forced-swim/fatigue) survives a cell re-type.
    {
        MclqLayer& L = ensureLiquidLayer(mc, LiquidType::River);
        L.renderFlags[5] = MCLQ_TILE_FORCED_SWIM | MCLQ_TILE_HIDDEN;
        setLiquidCells(mc, LiquidType::River, 1ull << 5, 12.0f);
        CHECK(mc.liquidLayers[0].renderFlags[5] == (MCLQ_TILE_FORCED_SWIM | 0x04));
    }

    // The existing render path consumes the mirror: 3 wet cells -> 6 triangles.
    {
        Mesh m = buildLiquidMesh(mc, 32, 32);
        CHECK(m.vertices.size() == 81);
        CHECK(m.indices.size() == 3u * 2u * 3u);
    }

    // --- clear-all drops the layer, the MCNK flag and the mirror -------------
    clearLiquidCells(mc, LiquidType::River, ~0ull);
    CHECK(mc.liquidLayers.empty());
    CHECK(!(mc.flags & MCNK_LQ_RIVER));
    CHECK(!mc.hasLiquid);
    CHECK(mc.liquidType == LiquidType::None);
    CHECK(buildLiquidMesh(mc, 32, 32).vertices.empty());

    // --- two stacked layers stay in LQ-flag order; clearing the primary ------
    // --- promotes the next into the back-compat mirror -----------------------
    {
        MapChunk m2;
        setLiquidCells(m2, LiquidType::Magma, 0x1ull, 35.0f);   // magma first...
        setLiquidCells(m2, LiquidType::River, ~0ull, 15.0f);    // ...river must sort before it
        CHECK(m2.liquidLayers.size() == 2);
        CHECK(m2.liquidLayers[0].type == LiquidType::River);
        CHECK(m2.liquidLayers[1].type == LiquidType::Magma);
        CHECK(m2.liquidLayers[1].renderFlags[0] == 0x06);       // magma nibble
        CHECK(m2.liquidType == LiquidType::River);
        CHECK((m2.flags & (MCNK_LQ_RIVER | MCNK_LQ_MAGMA)) == (MCNK_LQ_RIVER | MCNK_LQ_MAGMA));

        clearLiquidCells(m2, LiquidType::River, ~0ull);
        CHECK(m2.liquidLayers.size() == 1);
        CHECK(m2.liquidType == LiquidType::Magma);              // promoted
        CHECK(m2.liquid.renderFlags[0] == 0x06);
        CHECK(!(m2.flags & MCNK_LQ_RIVER));
        CHECK(m2.flags & MCNK_LQ_MAGMA);
    }

    // --- whole-tile fill / wipe over 256 chunks -------------------------------
    {
        std::vector<MapChunk> tile(256);
        int enabled = fillTileWater(tile, LiquidType::River, 7.5f);
        CHECK(enabled == 256 * 64);
        bool allWet = true;
        for (const MapChunk& q : tile) {
            if (q.liquidLayers.size() != 1) { allWet = false; break; }
            for (int i = 0; i < 64; ++i)
                if (q.liquidLayers[0].renderFlags[i] != 0x04) allWet = false;
            if (!liquidTileRenders(q.liquid.renderFlags[63])) allWet = false;
        }
        CHECK(allWet);
        CHECK(fillTileWater(tile, LiquidType::River, 7.5f) == 0);   // nothing new

        int wiped = clearTileLiquid(tile, LiquidType::River);
        CHECK(wiped == 256);
        bool allDry = true;
        for (const MapChunk& q : tile)
            if (q.hasLiquid || !q.liquidLayers.empty() || (q.flags & MCNK_LQ_RIVER))
                allDry = false;
        CHECK(allDry);
        CHECK(clearTileLiquid(tile, LiquidType::River) == 0);
    }

    // --- tiltLiquid: angled plane through the lock point ----------------------
    {
        MapChunk tc;
        tc.position = {100.0f, 200.0f, 0.0f};   // NW corner world XY

        // No layer yet: tilt is a no-op.
        tiltLiquid(tc, LiquidType::River, {100.0f, 200.0f, 5.0f}, 0.0f, 30.0f);
        CHECK(tc.liquidLayers.empty());

        setLiquidCells(tc, LiquidType::River, ~0ull, 0.0f);
        tiltLiquid(tc, LiquidType::River, {100.0f, 200.0f, 5.0f}, 0.0f, 30.0f);
        // Orientation 0 = +X north; rows advance south (-X) at UNIT_SIZE, so
        // height(i,j) = 5 - i * UNIT_SIZE * tan(30 deg), independent of j.
        const float U   = static_cast<float>(UNIT_SIZE);
        const float t30 = static_cast<float>(std::tan(radians(30.0)));
        const MclqLayer& L = tc.liquidLayers[0];
        CHECK_APPROX(L.heights[0 * 9 + 0], 5.0f);                 // at the lock
        CHECK_APPROX(L.heights[0 * 9 + 5], 5.0f);                 // constant across a row
        CHECK_NEAR(L.heights[4 * 9 + 2], 5.0f - 4 * U * t30, 1e-3f);
        CHECK_NEAR(L.heights[8 * 9 + 0], 5.0f - 8 * U * t30, 1e-3f);
        CHECK_NEAR(L.heights[8 * 9 + 0], -14.2450f, 1e-2f);       // hand-computed
        // Envelope tracks the tilted plane (all 64 cells render).
        CHECK_APPROX(L.maxHeight, 5.0f);
        CHECK_NEAR(L.minHeight, -14.2450f, 1e-2f);
    }

    // --- cropBelowTerrain ------------------------------------------------------
    {
        MapChunk cc;
        cc.position = {0, 0, 2.0f};                       // terrain base 2, MCVT all 0
        setLiquidCells(cc, LiquidType::River, ~0ull, 5.0f);   // water above everywhere
        CHECK(cropBelowTerrain(cc, LiquidType::River) == 0);

        // Raise the terrain above the water at exactly cell (0,0)'s four
        // corners: outer MCVT vertices (0,0),(0,1),(1,0),(1,1) = 0,1,17,18.
        cc.heights[0] = 8.0f; cc.heights[1] = 8.0f;       // absolute 10 > 5
        cc.heights[17] = 8.0f; cc.heights[18] = 8.0f;
        int removed = cropBelowTerrain(cc, LiquidType::River);
        CHECK(removed == 1);
        CHECK(cc.liquidLayers[0].renderFlags[0] == 0x0F); // cropped
        CHECK(cc.liquidLayers[0].renderFlags[1] == 0x04); // neighbour keeps 2 wet corners
        CHECK(cc.liquidLayers[0].renderFlags[8] == 0x04);
        CHECK(cc.hasLiquid);                              // layer survives

        // A shoreline touch (liquid == terrain) keeps the cell.
        MapChunk sc;
        sc.position = {0, 0, 5.0f};
        setLiquidCells(sc, LiquidType::River, ~0ull, 5.0f);
        CHECK(cropBelowTerrain(sc, LiquidType::River) == 0);

        // Fully buried water: every cell removed, layer + flag dropped.
        MapChunk bc;
        bc.position = {0, 0, 50.0f};
        setLiquidCells(bc, LiquidType::River, ~0ull, 5.0f);
        CHECK(cropBelowTerrain(bc, LiquidType::River) == 64);
        CHECK(bc.liquidLayers.empty());
        CHECK(!(bc.flags & MCNK_LQ_RIVER));
        CHECK(!bc.hasLiquid);
    }

    // --- autoDepth: shoreline 0, saturation 255, monotone in between ----------
    {
        MapChunk dc;
        dc.position = {0, 0, 0};                          // terrain absolute 0
        setLiquidCells(dc, LiquidType::River, ~0ull, 20.0f);
        MclqLayer& L = ensureLiquidLayer(dc, LiquidType::River);
        L.heights[0] = 0.0f;      // shoreline: liquid == terrain
        L.heights[1] = -3.0f;     // liquid under the ground clamps to 0 too
        L.heights[2] = 200.0f;    // saturates the ocean factor (200 * 2.55 > 255)

        autoDepth(dc, LiquidType::River, kOceanDepthFactor);
        CHECK(dc.liquidLayers[0].depth[0] == 0);
        CHECK(dc.liquidLayers[0].depth[1] == 0);
        CHECK(dc.liquidLayers[0].depth[2] == 255);
        CHECK(dc.liquidLayers[0].depth[40] == 51);        // 20 yd * 2.55
        CHECK(dc.liquidLayers[0].depth[0] < dc.liquidLayers[0].depth[40]);
        CHECK(dc.liquidLayers[0].depth[40] < dc.liquidLayers[0].depth[2]);
        CHECK(dc.liquid.depth[2] == 255);                 // mirror refreshed

        // The river preset saturates the same 20 yd column (20 * 12.75 = 255).
        autoDepth(dc, LiquidType::River, kRiverDepthFactor);
        CHECK(dc.liquidLayers[0].depth[40] == 255);

        // Magma carries texcoords, not depth: a documented no-op.
        MapChunk mg;
        mg.position = {0, 0, 0};
        setLiquidCells(mg, LiquidType::Magma, ~0ull, 30.0f);
        autoDepth(mg, LiquidType::Magma, kOceanDepthFactor);
        CHECK(mg.liquidLayers[0].depth[40] == 0);
    }

    // --- encodeMclq golden: river + magma = two 804-byte blocks ---------------
    MapChunk ec;
    ec.position = {0, 0, 0};
    setLiquidCells(ec, LiquidType::River, ~0ull, 15.0f);
    autoDepth(ec, LiquidType::River, kOceanDepthFactor);  // depth 38 (15 * 2.55)
    setLiquidCells(ec, LiquidType::Magma, 0x1ull, 35.0f); // only cell (0,0)
    CHECK(mcnkLiquidFlags(ec) == (MCNK_LQ_RIVER | MCNK_LQ_MAGMA));

    std::vector<uint8_t> blob = encodeMclq(ec);
    CHECK(blob.size() == 1608u);                          // 2 layers x 804 B
    {
        // River block at offset 0: min/max floats first.
        CHECK_APPROX(rdf(blob, 0), 15.0f);
        CHECK_APPROX(rdf(blob, 4), 15.0f);
        // Water vertex 0 = {depth, flow0Pct, flow1Pct, filler, float height}.
        CHECK(blob[8 + 0] == 38);
        CHECK(blob[8 + 1] == 0);
        CHECK(blob[8 + 2] == 0);
        CHECK(blob[8 + 3] == 0);
        CHECK_APPROX(rdf(blob, 8 + 4), 15.0f);
        // Last vertex (80) too.
        CHECK(blob[8 + 80 * 8] == 38);
        CHECK_APPROX(rdf(blob, 8 + 80 * 8 + 4), 15.0f);
        // 64 tile bytes at 8 + 648 = 656: all river.
        CHECK(blob[656] == 0x04);
        CHECK(blob[656 + 63] == 0x04);
        // nFlowvs at 720 is 0 and the two 40-byte SWFlowv records are zeroed.
        CHECK(rd32(blob, 720) == 0);
        bool flowZero = true;
        for (size_t i = 724; i < 804; ++i)
            if (blob[i] != 0) flowZero = false;
        CHECK(flowZero);

        // Magma block at the 804-byte stride: u16 s/t union, then the height.
        CHECK_APPROX(rdf(blob, 804 + 0), 35.0f);          // min: only cell 0 renders
        CHECK_APPROX(rdf(blob, 804 + 4), 35.0f);
        // Vertex (0,0): s=0, t=0. Vertex (0,1): s=32 (uv 0.375 at the 1.12
        // 3.0/256.0 scale), t=0, height 35 (covered by cell 0).
        CHECK(rd16(blob, 804 + 8 + 0) == 0);
        CHECK(rd16(blob, 804 + 8 + 2) == 0);
        CHECK(rd16(blob, 804 + 8 + 8) == 32);
        CHECK(rd16(blob, 804 + 8 + 8 + 2) == 0);
        CHECK_APPROX(rdf(blob, 804 + 8 + 8 + 4), 35.0f);
        // Vertex (1,0): s=0, t=32.
        CHECK(rd16(blob, 804 + 8 + 9 * 8) == 0);
        CHECK(rd16(blob, 804 + 8 + 9 * 8 + 2) == 32);
        // Tiles: cell 0 magma, cell 1 hidden. Flow tail zeroed here too.
        CHECK(blob[804 + 656] == 0x06);
        CHECK(blob[804 + 657] == 0x0F);
        CHECK(rd32(blob, 804 + 720) == 0);
    }

    // --- round-trip: encodeMclq + mcnkLiquidFlags through the MCNK parser -----
    {
        std::vector<uint8_t> hdr(128, 0);
        const uint32_t fl = mcnkLiquidFlags(ec);
        for (int i = 0; i < 4; i++) hdr[static_cast<size_t>(i)] = static_cast<uint8_t>((fl >> (8 * i)) & 0xFF);

        std::vector<uint8_t> body = hdr;
        chunk(body, "MCLQ", encodeMclq(ec));
        std::vector<uint8_t> adt;
        chunk(adt, "MCNK", body);

        auto parsed = parseChunks(adt);
        CHECK(parsed.size() == 1);
        const MapChunk& rt = parsed[0];
        CHECK(rt.liquidLayers.size() == 2);
        CHECK(rt.liquidLayers[0].type == LiquidType::River);
        CHECK(rt.liquidLayers[1].type == LiquidType::Magma);
        CHECK_APPROX(rt.liquidLayers[0].minHeight, 15.0f);
        CHECK_APPROX(rt.liquidLayers[0].maxHeight, 15.0f);
        CHECK_APPROX(rt.liquidLayers[0].heights[40], 15.0f);
        CHECK(rt.liquidLayers[0].depth[40] == 38);        // water depth survives
        CHECK(rt.liquidLayers[0].renderFlags[0] == 0x04);
        CHECK(rt.liquidLayers[0].renderFlags[63] == 0x04);
        CHECK_APPROX(rt.liquidLayers[1].heights[0], 35.0f);
        CHECK_APPROX(rt.liquidLayers[1].heights[80], 0.0f);   // outside cell 0
        CHECK(rt.liquidLayers[1].renderFlags[0] == 0x06);
        CHECK(rt.liquidLayers[1].renderFlags[63] == 0x0F);
        CHECK(rt.liquidLayers[1].depth[40] == 0);         // magma: texcoords, no depth
        CHECK(rt.hasLiquid);
        CHECK(rt.liquidType == LiquidType::River);
    }
}
