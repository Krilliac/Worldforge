#pragma once
// ---------------------------------------------------------------------------
// Shared types for the streaming multi-tile world: a tile coordinate (WDT block
// indices), a per-tile level-of-detail bucket, and the tile<->world helpers the
// streamer / LOD / pick modules agree on. Kept header-only and dependency-light
// so every world_* module can build against one definition.
//
// Axis convention matches coords.hpp/chunkCornerWorld: a tile's NW corner is its
// maximum world X (north) and Y (west); X/Y decrease moving south/east.
// ---------------------------------------------------------------------------
#include <cstdint>

#include "math.hpp"
#include "coords.hpp"

namespace wf {

// A map tile address: x = WDT column (the filename's first index, west),
// y = WDT row (second index, north). Valid range on a full map is [0,63].
struct TileCoord {
    int x = 0;
    int y = 0;
    bool operator==(const TileCoord& o) const { return x == o.x && y == o.y; }
    bool operator!=(const TileCoord& o) const { return !(*this == o); }
    bool operator<(const TileCoord& o) const { return (y != o.y) ? y < o.y : x < o.x; }
};

// Stable map key for a tile (y-major), handy for std::unordered_map.
inline int tileKey(TileCoord c) { return c.y * 64 + c.x; }

// Per-tile detail bucket chosen by distance from the camera.
//   Full   -> draw the full-resolution ADT terrain (+ doodads/WMOs)
//   Far    -> draw the low-res WDL tile mesh only
//   Culled -> skip entirely (too far, or outside the view frustum)
enum class TileLod { Full, Far, Culled };

// World-space NW corner of ADT tile (x,y) at z=0 (same as chunkCornerWorld at
// row=col=0). North is +X, west is +Y.
inline Vec3 tileCornerWorld(int x, int y) {
    return { static_cast<float>((32.0 - y) * TILE_SIZE),
             static_cast<float>((32.0 - x) * TILE_SIZE),
             0.0f };
}

// World-space centre of ADT tile (x,y) at z=0 (half a tile in from the corner).
inline Vec3 tileCenterWorld(int x, int y) {
    const Vec3 c = tileCornerWorld(x, y);
    return { c.x - static_cast<float>(TILE_SIZE) * 0.5f,
             c.y - static_cast<float>(TILE_SIZE) * 0.5f,
             0.0f };
}

// Tile that contains world position p (inverse of tileCornerWorld). Floors, so
// it lands on the tile whose [corner-TILE, corner] box holds p.
inline TileCoord worldToTile(Vec3 p) {
    return { static_cast<int>(32.0 - p.y / TILE_SIZE),
             static_cast<int>(32.0 - p.x / TILE_SIZE) };
}

} // namespace wf
