#pragma once
// ---------------------------------------------------------------------------
// navmesh: parse a mangos-zero `.mmtile` (Detour navigation mesh tile) far
// enough to render its polygons as a wireframe overlay -- the one server output
// WorldForge cannot reconstruct from the client assets (Recast/Detour is the
// extractor's job). Feeds DebugCategory::NavMesh / NavPath.
//
// Format verified vs mangos-zero MoveMapSharedDefines.h + recastnavigation
// DetourNavMesh.h: a 20-byte MmapTileHeader, then a 100-byte dtMeshHeader, then
// the vertex array (float[3] each) and the polygon array (dtPoly, 32 bytes).
//
// Detour space is Y-up: a vertex is stored (rx, ry, rz). mangos builds it from
// WoW world coords via world(x,y,z) -> recast(y, z, x); addNavMesh undoes that
// so polygons draw in WoW Z-up world space.
// ---------------------------------------------------------------------------
#include <array>
#include <cstdint>
#include <vector>

#include "math.hpp"
#include "debugdraw.hpp"

namespace wf {

struct NavPoly {
    uint8_t                  vertCount = 0;   // valid entries in verts (<= 6)
    std::array<uint16_t, 6>  verts{};         // indices into NavTile::verts
    uint16_t                 flags = 0;       // NavTerrain bits
    uint8_t                  area  = 0;       // low 6 bits of areaAndType
};

struct NavTile {
    int   tileX = 0, tileY = 0;
    Vec3  bmin, bmax;                 // tile AABB (Detour space)
    std::vector<Vec3>    verts;       // Detour-space vertices (rx,ry,rz)
    std::vector<NavPoly> polys;
};

// Parse a .mmtile buffer. Throws std::runtime_error on a bad magic/version.
NavTile parseMmTile(const std::vector<uint8_t>& buf);

// Convert a Detour-space vertex (rx,ry,rz) to WoW Z-up world space.
inline Vec3 navToWorld(const Vec3& r) { return { r.z, r.x, r.y }; }

// Draw each polygon's edges into a DebugDraw (converted to world space).
void addNavMesh(DebugDraw& dd, const NavTile& tile, Rgba color,
                DebugCategory cat = DebugCategory::NavMesh);

} // namespace wf
