#include "test.hpp"
#include "world_lod.hpp"
#include "world_types.hpp"
#include "coords.hpp"

#include <cmath>
#include <vector>

using namespace wf;

void test_world_lod() {
    std::printf("[world_lod]\n");

    // Camera sitting exactly on the centre of tile (32,32): zero distance -> Full.
    const TileCoord home{32, 32};
    Vec3 cam = tileCenterWorld(home.x, home.y);
    CHECK_APPROX(tileDistance(cam, home), 0.0f);
    CHECK(selectLod(cam, home) == TileLod::Full);

    // tileDistance must match the coords formula directly (full 3D length).
    {
        TileCoord t{40, 18};
        Vec3 c{ 1234.0f, -567.0f, 89.0f };
        Vec3 ctr = tileCenterWorld(t.x, t.y);
        float dx = c.x - ctr.x, dy = c.y - ctr.y, dz = c.z - ctr.z;
        CHECK_APPROX(tileDistance(c, t), std::sqrt(dx*dx + dy*dy + dz*dz));
    }

    // Stepping tile.x by N moves the tile centre by N*TILE_SIZE along world Y,
    // so distance from (32,32) is a clean multiple of the tile size.
    // ~2 tiles (1066.67 < 1100) -> Full; ~3 tiles (1600) -> Far.
    const TileCoord near2{34, 32};   // 2 * 533.33 = 1066.67 yds
    const TileCoord near3{35, 32};   // 3 * 533.33 = 1600.00 yds
    CHECK_APPROX(tileDistance(cam, near2), static_cast<float>(2.0 * TILE_SIZE));
    CHECK(tileDistance(cam, near2) < 1100.0f);
    CHECK(selectLod(cam, near2) == TileLod::Full);
    CHECK_APPROX(tileDistance(cam, near3), static_cast<float>(3.0 * TILE_SIZE));
    CHECK(selectLod(cam, near3) == TileLod::Far);

    // Very far tile (well past farDist) -> Culled.
    const TileCoord faraway{50, 10};  // tens of tiles away
    CHECK(tileDistance(cam, faraway) > 6000.0f);
    CHECK(selectLod(cam, faraway) == TileLod::Culled);

    // Band boundaries are inclusive (<=): a tile sitting exactly on fullDist is
    // still Full, and one exactly on farDist is still Far.
    {
        // Place the camera so the centre is at the tile centre but offset purely
        // in Z by the threshold distance -- distance == threshold exactly.
        Vec3 onFull = tileCenterWorld(home.x, home.y) + Vec3{0, 0, 1100.0f};
        Vec3 onFar  = tileCenterWorld(home.x, home.y) + Vec3{0, 0, 6000.0f};
        CHECK(selectLod(onFull, home) == TileLod::Full);
        CHECK(selectLod(onFar,  home) == TileLod::Far);
    }

    // Custom thresholds are honoured: shrink the full band so the 2-tile
    // neighbour drops to Far.
    {
        LodThresholds tight{ 500.0f, 6000.0f };
        CHECK(selectLod(cam, near2, tight) == TileLod::Far);
    }

    // classify(): one result per input, in order, with matching lod/dist.
    std::vector<TileCoord> tiles = { home, near2, near3, faraway };
    auto results = classify(cam, tiles);
    CHECK(results.size() == tiles.size());
    for (size_t i = 0; i < tiles.size(); ++i) {
        CHECK(results[i].tile == tiles[i]);
        CHECK_APPROX(results[i].dist, tileDistance(cam, tiles[i]));
        CHECK(results[i].lod == selectLod(cam, tiles[i]));
    }
    CHECK(results[0].lod == TileLod::Full);
    CHECK(results[1].lod == TileLod::Full);
    CHECK(results[2].lod == TileLod::Far);
    CHECK(results[3].lod == TileLod::Culled);

    // Empty input -> empty output (no crash, exact one-per-input contract).
    CHECK(classify(cam, {}).empty());

    // --- object draw-distance fade (T3.4) -----------------------------------
    {
        // fadeAlpha: 1 up to fadeStart, linear ramp to 0 at cullDist.
        CHECK_APPROX(fadeAlpha(0.0f,   100.0f, 200.0f), 1.0f);
        CHECK_APPROX(fadeAlpha(100.0f, 100.0f, 200.0f), 1.0f);   // at fadeStart
        CHECK_APPROX(fadeAlpha(150.0f, 100.0f, 200.0f), 0.5f);   // midpoint
        CHECK_APPROX(fadeAlpha(200.0f, 100.0f, 200.0f), 0.0f);   // at cull
        CHECK_APPROX(fadeAlpha(999.0f, 100.0f, 200.0f), 0.0f);   // beyond cull

        // Monotonically non-increasing across the ramp.
        float prev = 1.0f;
        for (int i = 0; i <= 20; ++i) {
            float a = fadeAlpha(100.0f + i * 5.0f, 100.0f, 200.0f);
            CHECK(a <= prev + 1e-6f);
            prev = a;
        }

        // Degenerate cullDist <= fadeStart -> hard cut at cullDist.
        CHECK_APPROX(fadeAlpha(150.0f, 200.0f, 200.0f), 1.0f);   // d < cull -> visible
        CHECK_APPROX(fadeAlpha(250.0f, 200.0f, 200.0f), 0.0f);   // d >= cull -> gone

        // Doodads fade out much closer than WMOs at the same distance.
        DrawDistances dd;
        CHECK_APPROX(doodadAlpha(0.0f, dd), 1.0f);
        CHECK_APPROX(doodadAlpha(350.0f, dd), 0.0f);             // past doodadCull
        CHECK(wmoAlpha(350.0f, dd) > 0.9f);                      // WMO still fully up
        CHECK(doodadAlpha(250.0f, dd) < wmoAlpha(250.0f, dd));   // doodad fading, WMO not
        CHECK_APPROX(wmoAlpha(1000.0f, dd), 0.0f);               // WMO gone at its cull
    }

    // --- tile Full<->WDL cross-fade weight ----------------------------------
    {
        // Full weight 1 until `band` before fullDist, ramp to 0 at fullDist.
        CHECK_APPROX(tileFullWeight(500.0f,  1000.0f, 200.0f), 1.0f);   // well inside
        CHECK_APPROX(tileFullWeight(800.0f,  1000.0f, 200.0f), 1.0f);   // at band start
        CHECK_APPROX(tileFullWeight(900.0f,  1000.0f, 200.0f), 0.5f);   // mid-band
        CHECK_APPROX(tileFullWeight(1000.0f, 1000.0f, 200.0f), 0.0f);   // at fullDist
        CHECK_APPROX(tileFullWeight(1500.0f, 1000.0f, 200.0f), 0.0f);   // beyond -> WDL only
    }
}
