#include "test.hpp"
#include "coords.hpp"

using namespace wf;

void test_coords() {
    std::printf("[coords]\n");

    // Map north-west corner is (+32T, +32T) ~ 17066.6667 (float-precision eps).
    CHECK_NEAR(ZEROPOINT, 17066.6667, 1e-3);

    // Tile-index formula: world X just south of the NW corner is tile 0;
    // X = 0 is tile 32; far south approaches tile 63.
    CHECK(tileIndexFromCoord(ZEROPOINT - 1.0) == 0);
    CHECK(tileIndexFromCoord(0.0)             == 32);
    CHECK(tileIndexFromCoord(-(ZEROPOINT - 1.0)) == 63);

    // placement <-> world round trip is exact.
    {
        Vec3 stored{ 12345.0f, 50.0f, 23456.0f };
        Vec3 world = placementToWorld(stored);
        Vec3 back  = worldToPlacement(world);
        CHECK_APPROX(back.x, stored.x);
        CHECK_APPROX(back.y, stored.y);
        CHECK_APPROX(back.z, stored.z);
    }

    // Semantic anchor: stored (0,*,0) is the map NW corner in world space.
    {
        Vec3 world = placementToWorld({0, 100, 0});
        CHECK_NEAR(world.x, ZEROPOINT, 0.02);  // north edge (float precision)
        CHECK_NEAR(world.y, ZEROPOINT, 0.02);  // west edge
        CHECK_APPROX(world.z, 100);            // height passes through
    }

    // A placement inside tile (blockX,blockY) must resolve back to that tile.
    {
        int bx = 40, by = 18;
        // Pick a world point near the centre of that tile.
        double worldX = (32.0 - bx) * TILE_SIZE - TILE_SIZE * 0.5; // north
        double worldY = (32.0 - by) * TILE_SIZE - TILE_SIZE * 0.5; // west
        CHECK(tileIndexFromCoord(worldX) == bx);
        CHECK(tileIndexFromCoord(worldY) == by);
        // And the inverse placement transform lands in the same tile.
        Vec3 stored = worldToPlacement({(float)worldX, (float)worldY, 0});
        Vec3 world  = placementToWorld(stored);
        CHECK(tileIndexFromCoord(world.x) == bx);
        CHECK(tileIndexFromCoord(world.y) == by);
    }
}
