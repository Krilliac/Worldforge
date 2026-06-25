#include "test.hpp"
#include "tile_streamer.hpp"

#include <algorithm>

using namespace wf;

namespace {
// True if `v` contains `c` (order-independent membership check).
bool has(const std::vector<TileCoord>& v, TileCoord c) {
    return std::find(v.begin(), v.end(), c) != v.end();
}
// True if `v` is sorted by TileCoord::operator< (y-major then x).
bool sorted(const std::vector<TileCoord>& v) {
    for (size_t i = 1; i < v.size(); ++i)
        if (v[i] < v[i - 1]) return false;
    return true;
}
} // namespace

void test_tile_streamer() {
    std::printf("[tile_streamer]\n");

    // --- desired: radius-1 disk in open space is the full 3x3 block ---------
    {
        TileStreamer s(1);
        CHECK(s.radius() == 1);
        std::vector<TileCoord> d = s.desired({32, 32});
        CHECK(d.size() == 9);
        CHECK(sorted(d));                       // deterministic, y-major then x
        CHECK(has(d, TileCoord{31, 31}));
        CHECK(has(d, TileCoord{32, 32}));       // focus is included
        CHECK(has(d, TileCoord{33, 33}));
        CHECK(!has(d, TileCoord{34, 32}));      // just outside the disk
        // First entry must be the NW-most (smallest y, then smallest x).
        CHECK(d.front() == (TileCoord{31, 31}));
        CHECK(d.back()  == (TileCoord{33, 33}));
    }

    // --- plan from empty: load everything, evict nothing -------------------
    {
        TileStreamer s(1);
        auto p = s.plan({32, 32});
        CHECK(p.toLoad.size() == 9);
        CHECK(p.toEvict.empty());
        CHECK(s.residentCount() == 0);          // plan() is pure: no side effect

        // Apply the plan: mark every desired tile loaded.
        for (TileCoord c : p.toLoad) s.markLoaded(c);
        CHECK(s.residentCount() == 9);
        CHECK(s.isResident({32, 32}));
        CHECK(sorted(s.resident()));

        // Re-planning the same focus now asks for nothing.
        auto p2 = s.plan({32, 32});
        CHECK(p2.toLoad.empty());
        CHECK(p2.toEvict.empty());

        // --- focus shifts east by one tile: one new column in, one column out -
        // x range 31..33 -> 32..34: load column x=34 (3 tiles), evict x=31 (3).
        auto p3 = s.plan({33, 32});
        CHECK(p3.toLoad.size() == 3);
        CHECK(p3.toEvict.size() == 3);
        for (TileCoord c : p3.toLoad)  CHECK(c.x == 34);
        for (TileCoord c : p3.toEvict) CHECK(c.x == 31);

        // Carry out the move; the resident set tracks it exactly.
        for (TileCoord c : p3.toEvict) s.markEvicted(c);
        for (TileCoord c : p3.toLoad)  s.markLoaded(c);
        CHECK(s.residentCount() == 9);
        CHECK(!s.isResident({31, 31}));
        CHECK(s.isResident({34, 33}));
    }

    // --- desired clamps at the map corner ----------------------------------
    {
        TileStreamer s(1);
        std::vector<TileCoord> d = s.desired({0, 0});
        // Only the in-bounds quadrant survives: (0,0),(1,0),(0,1),(1,1).
        CHECK(d.size() == 4);
        CHECK(has(d, TileCoord{0, 0}));
        CHECK(has(d, TileCoord{1, 1}));
        CHECK(!has(d, TileCoord{-1, 0}));       // never emit off-map tiles
        // Far corner (63,63) clamps the other way -> 4 tiles too.
        CHECK(s.desired({63, 63}).size() == 4);
    }

    // --- radius 0 is just the focus; radius 2 is a 5x5 block ----------------
    {
        TileStreamer s(0);
        CHECK(s.desired({10, 10}).size() == 1);
        s.setRadius(2);
        CHECK(s.radius() == 2);
        CHECK(s.desired({10, 10}).size() == 25);
        // setRadius clamps negatives to 0.
        s.setRadius(-5);
        CHECK(s.radius() == 0);
        CHECK(s.desired({10, 10}).size() == 1);
    }

    // --- default radius is 2 -----------------------------------------------
    {
        TileStreamer s;
        CHECK(s.radius() == 2);
    }
}
