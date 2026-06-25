#pragma once
// ---------------------------------------------------------------------------
// Multi-tile terrain picking: cast a world-space ray against the loaded ADT
// tiles' meshes and report the NEAREST surface hit (which tile, where, how far).
// The per-mesh ray/triangle work is reused from gizmo.hpp (pickMesh); this layer
// only walks the tile set and keeps the closest result, so viewport selection /
// terrain sculpting can ask "what tile + point is under this cursor ray".
// ---------------------------------------------------------------------------
#include <vector>

#include "math.hpp"          // Vec3
#include "world_types.hpp"   // TileCoord
#include "terrain.hpp"       // Mesh
#include "gizmo.hpp"         // Ray, pickMesh

namespace wf {

// A tile's renderable mesh paired with its WDT address. The mesh is borrowed
// (caller owns it); pointer must outlive the pickWorld() call.
struct PickTile {
    TileCoord    coord;
    const Mesh*  mesh = nullptr;
};

// Result of a world pick. `dist` is the Euclidean distance from the ray origin
// to the hit point (== |point - ray.origin|), so callers can compare/order hits
// without re-deriving the ray parameter.
struct WorldHit {
    bool      hit = false;
    TileCoord tile;
    Vec3      point;
    float     dist = 0.0f;
};

// Nearest surface hit across all tiles. Skips null/empty meshes; returns
// hit=false when nothing is struck (including an empty list).
WorldHit pickWorld(const Ray& ray, const std::vector<PickTile>& tiles);

} // namespace wf
