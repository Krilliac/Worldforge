#pragma once
// ---------------------------------------------------------------------------
// WDL distant-terrain mesh: turn a parsed Wdl low-res heightfield into a
// renderable world-space triangle mesh -- the geometry the client draws for the
// far horizon (everything past the loaded ADT ring). One present tile becomes a
// 17x17 vertex grid (16x16 quads, two tris each) spanning a full ADT tile, in
// the SAME world XY footprint as buildTileMesh() so a WDL tile sits exactly
// under its ADT tile (only the resolution differs). Heights are the int16 yard
// values straight out of MARE's outer grid; per-vertex normals come from the
// cross of the grid tangents so the distant terrain still lights.
// ---------------------------------------------------------------------------
#include "math.hpp"
#include "terrain.hpp"   // Mesh / Vertex
#include "wdl.hpp"

namespace wf {

// Build ONE present tile's 17x17 world-space mesh. tileX/tileY are the WDL grid
// (== ADT filename) indices. Returns an empty Mesh if the tile is absent.
Mesh buildWdlTileMesh(const Wdl& wdl, int tileX, int tileY);

// Merge every present tile's mesh into one combined world mesh (indices offset
// per tile, exactly like buildTileMesh in terrain.cpp).
Mesh buildWdlWorldMesh(const Wdl& wdl);

}  // namespace wf
