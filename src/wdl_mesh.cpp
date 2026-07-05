// ---------------------------------------------------------------------------
// WDL distant-terrain mesh builder. See wdl_mesh.hpp for the format/contract.
// ---------------------------------------------------------------------------
#include "wdl_mesh.hpp"

#include "coords.hpp"

namespace wf {

namespace {
// A WDL outer grid is 17x17 over a whole ADT tile, so the cell spacing is the
// full tile divided into 16 cells (== one MCNK side). This is exactly the
// CHUNK_SIZE the ADT chunk grid uses, which is why a WDL vertex (i,j) lands on
// the NW corner of ADT chunk (i,j).
constexpr float kWdlStep = static_cast<float>(CHUNK_SIZE);
}  // namespace

Mesh buildWdlTileMesh(const Wdl& wdl, int tileX, int tileY) {
    Mesh mesh;
    if (!wdl.tilePresent(tileX, tileY)) return mesh;  // absent -> empty

    constexpr int O = Wdl::OUTER;   // 17

    // Tile NW corner in world space: row=0,col=0 of chunkCornerWorld is the
    // tile's max-X (north) / max-Y (west) corner, same as the ADT path.
    // VERIFY-FLAGGED: outer-grid axis mapping. We mirror buildChunkMesh's MCVT
    // convention -- grid index i = north-south (0 = north edge, advancing south
    // -> -X), j = west-east (0 = west edge, advancing east -> -Y) -- so a WDL
    // tile lines up vertex-for-corner with its ADT tile. Not yet checked against
    // a real .wdl's MARE row order.
    const Vec3 corner = chunkCornerWorld(tileX, tileY, /*row*/0, /*col*/0, 0.0f);

    auto idx = [](int i, int j) { return i * O + j; };  // 0..288, row-major

    mesh.vertices.resize(static_cast<size_t>(O) * O);
    for (int i = 0; i < O; ++i) {
        for (int j = 0; j < O; ++j) {
            Vertex& v = mesh.vertices[idx(i, j)];
            v.position = {
                corner.x - i * kWdlStep,                 // X north: south -> lower
                corner.y - j * kWdlStep,                 // Y west:  east  -> lower
                static_cast<float>(wdl.height(tileX, tileY, i, j))  // int16 yards
            };
            v.normal = {0.0f, 0.0f, 1.0f};               // filled in below
        }
    }

    // Per-vertex normals from the cross of the two grid tangents. World tangents:
    // increasing i moves -X, increasing j moves -Y; cross(d/di, d/dj) of those
    // base directions is +Z, so a flat field yields a clean upward normal.
    auto sampleZ = [&](int i, int j) {
        if (i < 0) i = 0; else if (i >= O) i = O - 1;
        if (j < 0) j = 0; else if (j >= O) j = O - 1;
        return mesh.vertices[idx(i, j)].position.z;
    };
    for (int i = 0; i < O; ++i) {
        for (int j = 0; j < O; ++j) {
            // Central differences; clamped at edges so border verts get one-sided
            // slopes rather than reading out of grid.
            float dzdi = (sampleZ(i + 1, j) - sampleZ(i - 1, j));
            float dzdj = (sampleZ(i, j + 1) - sampleZ(i, j - 1));
            // di spans the X axis (-step per i), dj the Y axis (-step per j).
            float di = (i > 0 && i < O - 1) ? 2.0f * kWdlStep : kWdlStep;
            float dj = (j > 0 && j < O - 1) ? 2.0f * kWdlStep : kWdlStep;
            Vec3 ti{-kWdlStep, 0.0f, dzdi / di * kWdlStep};   // tangent along i (north-south)
            Vec3 tj{0.0f, -kWdlStep, dzdj / dj * kWdlStep};   // tangent along j (west-east)
            // cross(ti, tj) points +Z for a flat field; normalize for lighting.
            mesh.vertices[idx(i, j)].normal = normalize(cross(ti, tj));
        }
    }

    // 16x16 quads, two triangles each. Wind so the front face is +Z up (CCW seen
    // from above), matching buildLiquidMesh's TL,TR,BR / TL,BR,BL order. The cell
    // grid maps 1:1 onto the tile's 16x16 MCNK grid (cell (i,j) spans chunk
    // row i, col j -- see the vertex/corner note above), so a chunk holed in
    // MAHO simply emits no quad: the horizon terrain shows the same hole the
    // full-res ADT does.
    mesh.indices.reserve(static_cast<size_t>(O - 1) * (O - 1) * 2 * 3);
    for (int i = 0; i < O - 1; ++i) {
        for (int j = 0; j < O - 1; ++j) {
            if (wdlChunkIsHole(wdl, tileX, tileY, i, j)) continue;   // holed MCNK
            uint32_t TL = static_cast<uint32_t>(idx(i,     j));
            uint32_t TR = static_cast<uint32_t>(idx(i,     j + 1));
            uint32_t BL = static_cast<uint32_t>(idx(i + 1, j));
            uint32_t BR = static_cast<uint32_t>(idx(i + 1, j + 1));
            mesh.indices.insert(mesh.indices.end(), {TL, TR, BR});
            mesh.indices.insert(mesh.indices.end(), {TL, BR, BL});
        }
    }
    return mesh;
}

Mesh buildWdlWorldMesh(const Wdl& wdl) {
    Mesh world;
    for (int y = 0; y < Wdl::DIM; ++y) {
        for (int x = 0; x < Wdl::DIM; ++x) {
            if (!wdl.tilePresent(x, y)) continue;
            Mesh m = buildWdlTileMesh(wdl, x, y);
            uint32_t base = static_cast<uint32_t>(world.vertices.size());
            world.vertices.insert(world.vertices.end(), m.vertices.begin(), m.vertices.end());
            for (uint32_t idx : m.indices) world.indices.push_back(base + idx);
        }
    }
    return world;
}

}  // namespace wf
