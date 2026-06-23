#include "test.hpp"
#include "debugdraw.hpp"

#include <vector>

using namespace wf;

void test_debugdraw() {
    std::printf("[debugdraw]\n");

    const Rgba red{255, 0, 0, 255};

    // --- primitives land in the right category bucket -----------------------
    {
        DebugDraw dd;
        dd.line({0,0,0}, {1,0,0}, red, DebugCategory::Waypoint);
        dd.triangle({0,0,0}, {1,0,0}, {0,1,0}, red, DebugCategory::Collision);
        dd.point({5,5,5}, red, DebugCategory::Marker);
        CHECK(dd.categoryBuffers(DebugCategory::Waypoint).lines.size() == 2);
        CHECK(dd.categoryBuffers(DebugCategory::Collision).tris.size() == 3);
        CHECK(dd.categoryBuffers(DebugCategory::Marker).points.size() == 1);
        // First line vertex carries the endpoint + colour.
        CHECK_APPROX(dd.categoryBuffers(DebugCategory::Waypoint).lines[0].pos.x, 0.0f);
        CHECK(dd.categoryBuffers(DebugCategory::Waypoint).lines[0].color.r == 255);
    }

    // --- AABB = 12 edges = 24 line vertices ---------------------------------
    {
        DebugDraw dd;
        dd.aabb({-1,-1,-1}, {1,1,1}, red, DebugCategory::Trigger);
        CHECK(dd.categoryBuffers(DebugCategory::Trigger).lines.size() == 24);
    }

    // --- sphere: 3 circles of `segments` segments each ----------------------
    {
        DebugDraw dd;
        dd.sphere({0,0,0}, 2.0f, red, DebugCategory::Trigger, 16);
        // 3 circles * 16 segments * 2 verts/segment.
        CHECK(dd.categoryBuffers(DebugCategory::Trigger).lines.size() == 3u * 16u * 2u);
    }

    // --- waypoint path: 3 nodes -> 2 segments, + a cross (3 lines) per node --
    {
        DebugDraw dd;
        std::vector<Vec3> pts = { {0,0,0}, {10,0,0}, {10,10,0} };
        dd.path(pts, red, DebugCategory::Waypoint, /*markers*/true);
        // 2 connecting segments + 3 nodes * 3 cross lines = 5 lines -> 10 verts.
        CHECK(dd.categoryBuffers(DebugCategory::Waypoint).lines.size() == (2u + 3u * 3u) * 2u);
        // Without markers, just the 2 connecting segments.
        DebugDraw dd2;
        dd2.path(pts, red, DebugCategory::NavPath, /*markers*/false);
        CHECK(dd2.categoryBuffers(DebugCategory::NavPath).lines.size() == 2u * 2u);
    }

    // --- wireframe dedups shared edges --------------------------------------
    {
        Mesh m;
        // Two triangles sharing edge (1,2): a quad. Unique edges = 5.
        m.vertices = { {{0,0,0},{0,0,1}}, {{1,0,0},{0,0,1}},
                       {{0,1,0},{0,0,1}}, {{1,1,0},{0,0,1}} };
        m.indices = { 0,1,2,  1,3,2 };
        DebugDraw dd;
        dd.wireframe(m, red, DebugCategory::TerrainWire);
        CHECK(dd.categoryBuffers(DebugCategory::TerrainWire).lines.size() == 5u * 2u);
    }

    // --- normals: one line per vertex ---------------------------------------
    {
        Mesh m;
        m.vertices = { {{0,0,0},{0,0,1}}, {{1,0,0},{0,0,1}} };
        DebugDraw dd;
        dd.normals(m, 2.0f, red, DebugCategory::Normal);
        CHECK(dd.categoryBuffers(DebugCategory::Normal).lines.size() == 2u * 2u);
        // Tip of the first normal = pos + normal*len.
        CHECK_APPROX(dd.categoryBuffers(DebugCategory::Normal).lines[1].pos.z, 2.0f);
    }

    // --- frustum: near rect + far rect + 4 connectors = 12 edges ------------
    {
        DebugDraw dd;
        dd.frustum({0,0,0}, {1,0,0}, {0,-1,0}, {0,0,1}, 60.0, 1.5, 1.0, 100.0,
                   red, DebugCategory::Frustum);
        CHECK(dd.categoryBuffers(DebugCategory::Frustum).lines.size() == 12u * 2u);
    }

    // --- stats count only enabled categories, then clear empties everything --
    {
        DebugDraw dd;
        dd.aabb({-1,-1,-1}, {1,1,1}, red, DebugCategory::Trigger);   // 12 lines
        dd.triangle({0,0,0},{1,0,0},{0,1,0}, red, DebugCategory::Collision);
        dd.point({0,0,0}, red, DebugCategory::Marker);
        DebugDraw::Stats s = dd.stats();
        CHECK(s.lines == 12 && s.triangles == 1 && s.points == 1);

        dd.setCategoryEnabled(DebugCategory::Trigger, false);
        CHECK(dd.stats().lines == 0);                 // trigger layer hidden
        CHECK(dd.stats().triangles == 1);             // others still counted
        CHECK(!dd.categoryEnabled(DebugCategory::Trigger));

        dd.clear();
        CHECK(dd.categoryBuffers(DebugCategory::Collision).tris.empty());
    }
}
