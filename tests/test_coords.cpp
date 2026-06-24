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

    // --- REAL-DATA PIN: MCNK header origin + IndexX/IndexY axis assignment ---
    // Cross-checked against the actual MCNK header position (offset 0x68) of the
    // first chunk of Azeroth_32_48.adt in the 1.12.1 client:
    //   filename "Azeroth_32_48" => blockX(=32) is the WEST tile index,
    //                               blockY(=48) is the NORTH tile index.
    //   chunk Index(0,0) header position == (-8533.334, 0.000, z).
    // This pins both the per-tile world origin and the IndexX=col/IndexY=row map.
    {
        Vec3 c = chunkCornerWorld(/*blockX=*/32, /*blockY=*/48,
                                  /*row=IndexY=*/0, /*col=IndexX=*/0, /*z=*/0.0f);
        CHECK_NEAR(c.x, (32.0 - 48) * TILE_SIZE, 0.01);   // north = -8533.333
        CHECK_NEAR(c.y, (32.0 - 32) * TILE_SIZE, 0.01);   // west  =     0.000
        CHECK_NEAR(c.x, -8533.333, 0.01);
        CHECK_NEAR(c.y,     0.000, 0.01);

        // IndexX advances WEST (-Y), IndexY advances SOUTH (-X), each by CHUNK.
        Vec3 cx = chunkCornerWorld(32, 48, /*row=*/0, /*col=IndexX=*/1, 0.0f);
        Vec3 cy = chunkCornerWorld(32, 48, /*row=IndexY=*/1, /*col=*/0, 0.0f);
        CHECK_NEAR(cx.y, -CHUNK_SIZE, 0.01);  // IndexX -> -Y (west)
        CHECK_APPROX(cx.x, c.x);              // ...leaves north unchanged
        CHECK_NEAR(cy.x, (32.0 - 48) * TILE_SIZE - CHUNK_SIZE, 0.01); // IndexY -> -X
        CHECK_APPROX(cy.y, c.y);              // ...leaves west unchanged
    }

    // --- REAL-DATA PIN: an MDDF placement lands in its own ADT tile ----------
    // A real doodad (uid 214347) from Azeroth_32_48: raw stored placement coords
    // -> world (north, west, up) must put it on tile (north idx 48 = fileY).
    {
        Vec3 stored{ 17602.238f, 145.878f, 25611.637f };  // MDDF pos as stored
        Vec3 w = placementToWorld(stored);
        CHECK_NEAR(w.x, -8544.971, 0.05);   // north
        CHECK_NEAR(w.y,  -535.572, 0.05);   // west
        CHECK_APPROX(w.z, 145.878f);        // height passes through
        // north index == fileY(48); object sits ~2 yd over the west edge so its
        // west index may be fileX(32) or the next index -- assert it is on-tile
        // to within one index (legal edge straddle for an object at the border).
        CHECK(tileIndexFromCoord(w.x) == 48);
        CHECK(std::abs(tileIndexFromCoord(w.y) - 32) <= 1);
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
