#include "test.hpp"
#include "wdl_mesh.hpp"
#include "coords.hpp"

#include <cmath>

using namespace wf;

namespace {
// Build a synthetic Wdl with the listed tiles present. Each present tile gets a
// west-east ramp in its 17x17 outer grid: height(i,j) = base + j, so the slope
// is non-flat and the normals must tilt off vertical.
Wdl makeWdl(std::initializer_list<std::pair<int,int>> tiles, int base) {
    Wdl w;
    w.outer.assign(static_cast<size_t>(Wdl::DIM) * Wdl::DIM * Wdl::N, 0);
    for (auto [x, y] : tiles) {
        w.present[y * Wdl::DIM + x] = true;
        for (int i = 0; i < Wdl::OUTER; ++i)
            for (int j = 0; j < Wdl::OUTER; ++j)
                w.outer[(static_cast<size_t>(y) * Wdl::DIM + x) * Wdl::N + i * Wdl::OUTER + j] =
                    static_cast<int16_t>(base + j);
    }
    return w;
}
}  // namespace

void test_wdl_mesh() {
    std::printf("[wdl_mesh]\n");

    // Three present tiles, each a distinct ramp base.
    Wdl w = makeWdl({{30, 30}, {31, 30}, {30, 31}}, /*base*/100);
    CHECK(w.tileCount() == 3);

    // --- single present tile mesh ---
    Mesh t = buildWdlTileMesh(w, 30, 30);
    CHECK(t.vertices.size() == 17u * 17u);                  // full outer grid
    CHECK(t.indices.size() == 16u * 16u * 2u * 3u);         // 1536

    // Vertex (0,0) sits at the tile NW corner; world XY must match the coords
    // formula used by the ADT path (row=0,col=0 of chunkCornerWorld).
    {
        Vec3 corner = chunkCornerWorld(30, 30, /*row*/0, /*col*/0, 0.0f);
        const Vertex& v00 = t.vertices[0];
        CHECK_APPROX(v00.position.x, corner.x);
        CHECK_APPROX(v00.position.y, corner.y);
        CHECK_APPROX(v00.position.z, 100.0f + 0.0f);        // base + j(0)
    }

    // A non-corner vertex: grid (i=1,j=2) -> X steps south by 1 chunk (-X),
    // Y steps east by 2 chunks (-Y), Z = base + j.
    {
        Vec3 corner = chunkCornerWorld(30, 30, 0, 0, 0.0f);
        const Vertex& v = t.vertices[1 * 17 + 2];
        CHECK_APPROX(v.position.x, corner.x - 1.0f * static_cast<float>(CHUNK_SIZE));
        CHECK_APPROX(v.position.y, corner.y - 2.0f * static_cast<float>(CHUNK_SIZE));
        CHECK_APPROX(v.position.z, 100.0f + 2.0f);
    }

    // Every index in range; no degenerate triangle.
    bool inRange = true, noDegen = true;
    for (size_t i = 0; i < t.indices.size(); i += 3) {
        uint32_t a = t.indices[i], b = t.indices[i + 1], c = t.indices[i + 2];
        if (a >= 289 || b >= 289 || c >= 289) inRange = false;
        if (a == b || b == c || a == c)       noDegen = false;
    }
    CHECK(inRange);
    CHECK(noDegen);

    // Normals: unit length, and the west-east ramp tilts them off vertical along
    // Y (increasing j -> -Y, rising Z, so the surface normal leans +Y).
    {
        const Vec3& n = t.vertices[8 * 17 + 8].normal;     // interior vertex
        CHECK_APPROX(std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z), 1.0f);
        CHECK(n.z > 0.0f);                                  // upward-facing
        CHECK(std::fabs(n.y) > 1e-3f);                      // tilted by the ramp
        CHECK(std::fabs(n.x) < 1e-4f);                      // flat along north-south
    }

    // --- absent tile yields an empty mesh ---
    {
        Mesh empty = buildWdlTileMesh(w, 0, 0);             // (0,0) not present
        CHECK(empty.vertices.empty());
        CHECK(empty.indices.empty());
        // Out-of-range index is also "absent".
        Mesh oob = buildWdlTileMesh(w, -1, 70);
        CHECK(oob.vertices.empty());
    }

    // --- world mesh = sum of present tiles ---
    {
        Mesh world = buildWdlWorldMesh(w);
        CHECK(world.vertices.size() == 3u * 17u * 17u);     // 3 present tiles
        CHECK(world.indices.size() == 3u * 16u * 16u * 2u * 3u);
        // Offset indices stay in range of the merged buffer.
        bool wOk = true;
        for (uint32_t i : world.indices) if (i >= world.vertices.size()) wOk = false;
        CHECK(wOk);

        // Tiles are merged in row-major (y, then x) order; the first block of 289
        // verts belongs to (30,30) so its vertex 0 matches the single-tile build.
        CHECK_APPROX(world.vertices[0].position.x, t.vertices[0].position.x);
        CHECK_APPROX(world.vertices[0].position.y, t.vertices[0].position.y);
    }

    // --- adjacency: tile (31,30) sits one full tile west (-Y) of (30,30) ---
    {
        Mesh east = buildWdlTileMesh(w, 31, 30);
        Vec3 c30 = chunkCornerWorld(30, 30, 0, 0, 0.0f);
        Vec3 c31 = chunkCornerWorld(31, 30, 0, 0, 0.0f);
        // blockX is the WEST index: a higher tileX is further west -> lower Y.
        CHECK_APPROX(east.vertices[0].position.y, c31.y);
        CHECK(c31.y < c30.y);
        CHECK_APPROX(east.vertices[0].position.x, c30.x);   // same north row
    }

    // --- empty Wdl -> empty world mesh ---
    {
        Wdl none;
        none.outer.assign(static_cast<size_t>(Wdl::DIM) * Wdl::DIM * Wdl::N, 0);
        Mesh wm = buildWdlWorldMesh(none);
        CHECK(wm.vertices.empty());
        CHECK(wm.indices.empty());
    }

    // --- MAHO hole: exactly the holed chunk's quad vanishes, rest identical ---
    {
        Wdl hw = makeWdl({{30, 30}}, /*base*/100);
        Mesh full = buildWdlTileMesh(hw, 30, 30);
        setWdlHole(hw, 30, 30, /*row*/3, /*col*/5, true);
        Mesh holed = buildWdlTileMesh(hw, 30, 30);

        // Vertex grid unchanged (positions AND normals), only quads drop.
        CHECK(holed.vertices.size() == full.vertices.size());
        bool vSame = true;
        for (size_t k = 0; k < full.vertices.size(); ++k) {
            const Vertex& a = full.vertices[k];
            const Vertex& b = holed.vertices[k];
            if (a.position.x != b.position.x || a.position.y != b.position.y ||
                a.position.z != b.position.z || a.normal.x != b.normal.x ||
                a.normal.y != b.normal.y || a.normal.z != b.normal.z) vSame = false;
        }
        CHECK(vSame);

        // One holed chunk = one quad = two triangles = 6 indices fewer.
        CHECK(holed.indices.size() == full.indices.size() - 6);

        // Cells emit row-major, so the holed stream must equal the full one
        // with cell (3,5)'s six indices spliced out and nothing else moved.
        const size_t cut = static_cast<size_t>(3 * 16 + 5) * 6;
        bool restSame = true;
        for (size_t k = 0; k < holed.indices.size(); ++k) {
            const size_t src = (k < cut) ? k : k + 6;
            if (holed.indices[k] != full.indices[src]) restSame = false;
        }
        CHECK(restSame);

        // And none of the surviving triangles touch ONLY the hole's quad: the
        // spliced-out 6 indices are exactly full's cell-(3,5) block.
        CHECK(full.indices[cut]     == 3u * 17u + 5u);          // TL of cell (3,5)
        CHECK(full.indices[cut + 1] == 3u * 17u + 6u);          // TR

        // The world mesh sees the hole too.
        Mesh world = buildWdlWorldMesh(hw);
        CHECK(world.indices.size() == full.indices.size() - 6);

        // Fully holing the tile leaves the grid but no geometry at all.
        for (int r = 0; r < 16; ++r)
            for (int c = 0; c < 16; ++c)
                setWdlHole(hw, 30, 30, r, c, true);
        Mesh gone = buildWdlTileMesh(hw, 30, 30);
        CHECK(gone.vertices.size() == 17u * 17u);
        CHECK(gone.indices.empty());
    }
}
