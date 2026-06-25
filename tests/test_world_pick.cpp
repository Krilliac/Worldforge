#include "test.hpp"
#include "world_pick.hpp"

#include <cmath>
#include <vector>

using namespace wf;

namespace {
// A flat axis-aligned quad (two triangles) centred at (cx,cy) at height z, with
// half-extent `half` in X and Y. Normals point straight up; only the positions
// matter for picking. Verts: 0=NW 1=NE 2=SE 3=SW, CCW from above.
Mesh quad(float cx, float cy, float z, float half) {
    Mesh m;
    m.vertices = {
        { {cx - half, cy + half, z}, {0, 0, 1} },
        { {cx + half, cy + half, z}, {0, 0, 1} },
        { {cx + half, cy - half, z}, {0, 0, 1} },
        { {cx - half, cy - half, z}, {0, 0, 1} },
    };
    m.indices = { 0, 1, 2, 0, 2, 3 };
    return m;
}

// Ray straight down (-Z) from high above (x,y).
Ray downRay(float x, float y) { return { {x, y, 1000.0f}, {0, 0, -1} }; }
} // namespace

void test_world_pick() {
    std::printf("[world_pick]\n");

    // Two tiles at distinct world XY footprints, same height plane z=50.
    Mesh meshA = quad(/*cx*/100.0f, /*cy*/100.0f, /*z*/50.0f, /*half*/10.0f);
    Mesh meshB = quad(/*cx*/300.0f, /*cy*/300.0f, /*z*/50.0f, /*half*/10.0f);
    TileCoord tA{ 1, 2 };
    TileCoord tB{ 5, 7 };
    std::vector<PickTile> tiles = { { tA, &meshA }, { tB, &meshB } };

    // --- a ray over tile A returns A at the right point/dist ---
    {
        WorldHit h = pickWorld(downRay(100.0f, 100.0f), tiles);
        CHECK(h.hit);
        CHECK(h.tile == tA);
        CHECK_APPROX(h.point.x, 100.0f);
        CHECK_APPROX(h.point.y, 100.0f);
        CHECK_APPROX(h.point.z, 50.0f);
        CHECK_APPROX(h.dist, 950.0f);          // 1000 - 50, straight down
    }

    // --- a ray over tile B returns B ---
    {
        WorldHit h = pickWorld(downRay(300.0f, 300.0f), tiles);
        CHECK(h.hit);
        CHECK(h.tile == tB);
        CHECK_APPROX(h.point.x, 300.0f);
        CHECK_APPROX(h.point.y, 300.0f);
    }

    // --- NEAREST chosen when two meshes stack under one ray ---
    {
        // Low quad (tile A footprint, z=50) and a higher quad at the SAME XY but
        // z=200, on a different tile. A downward ray hits both; the higher one is
        // closer to the (z=1000) origin, so it must win.
        Mesh high = quad(100.0f, 100.0f, 200.0f, 10.0f);
        TileCoord tHigh{ 9, 9 };
        std::vector<PickTile> stacked = { { tA, &meshA }, { tHigh, &high } };
        WorldHit h = pickWorld(downRay(100.0f, 100.0f), stacked);
        CHECK(h.hit);
        CHECK(h.tile == tHigh);
        CHECK_APPROX(h.point.z, 200.0f);
        CHECK_APPROX(h.dist, 800.0f);          // closer hit than the z=50 quad

        // Order independence: swapping the list yields the same nearest tile.
        std::vector<PickTile> swapped = { { tHigh, &high }, { tA, &meshA } };
        CHECK(pickWorld(downRay(100.0f, 100.0f), swapped).tile == tHigh);
    }

    // --- a ray missing everything -> no hit ---
    {
        WorldHit h = pickWorld(downRay(5000.0f, 5000.0f), tiles);
        CHECK(!h.hit);
    }

    // --- empty list -> no hit ---
    {
        std::vector<PickTile> none;
        CHECK(!pickWorld(downRay(100.0f, 100.0f), none).hit);
    }

    // --- null / empty meshes are skipped, not dereferenced ---
    {
        Mesh empty;   // no triangles
        std::vector<PickTile> degenerate = {
            { TileCoord{0, 0}, nullptr },
            { TileCoord{1, 1}, &empty },
            { tA, &meshA },
        };
        WorldHit h = pickWorld(downRay(100.0f, 100.0f), degenerate);
        CHECK(h.hit);
        CHECK(h.tile == tA);
    }
}
