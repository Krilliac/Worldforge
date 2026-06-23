#pragma once
// ---------------------------------------------------------------------------
// Vanilla WoW coordinate system. Verified against wowdev.wiki ADT/v18.
//
// World space (terrain + ALL network packets): right-handed, Z-up.
//   +X = north, +Y = west, +Z = height (0 = sea level). Origin at map centre.
//   Map north-west corner = (X, Y) = (+32T, +32T) = (+17066.67, +17066.67).
//   Tile (block) index for an axis value:  floor(32 - axis / TILE_SIZE).
//
// MDDF/MODF placement space differs from world space (this is the classic
// "everything is mirrored" trap). The wiki coordinate table gives:
//   placement = Left*x' + Up*y + Forward*z',  x' = 32T - sx,  z' = 32T - sz
// In world space (Forward=+X north, Left=+Y west, Up=+Z) this resolves to:
//   worldX(north) = 32T - sz
//   worldY(west)  = 32T - sx
//   worldZ(up)    = sy
// Implemented + round-trip unit-tested in tests/.
// ---------------------------------------------------------------------------
#include <cmath>
#include "math.hpp"

namespace wf {

constexpr double TILE_SIZE  = 533.0 + 1.0 / 3.0;  // 533.33333 yds / ADT tile
constexpr double CHUNK_SIZE = TILE_SIZE / 16.0;   // 33.33333 yds  / MCNK
constexpr double UNIT_SIZE  = CHUNK_SIZE / 8.0;   // 4.16666 yds   / cell
constexpr double ZEROPOINT  = 32.0 * TILE_SIZE;   // 17066.66656 origin offset

// Block (tile) index for a world-space axis value. wowdev.wiki formula.
inline int tileIndexFromCoord(double axis) {
    return static_cast<int>(std::floor(32.0 - (axis / TILE_SIZE)));
}

// MDDF/MODF stored placement position -> world space (north/west/up).
inline Vec3 placementToWorld(const Vec3& stored) {
    return {
        static_cast<float>(ZEROPOINT) - stored.z, // X north
        static_cast<float>(ZEROPOINT) - stored.x, // Y west
        stored.y                                  // Z up
    };
}

// Inverse: world space -> stored placement position (for authoring/round-trip).
inline Vec3 worldToPlacement(const Vec3& world) {
    return {
        static_cast<float>(ZEROPOINT) - world.y, // sx
        world.z,                                 // sy
        static_cast<float>(ZEROPOINT) - world.x  // sz
    };
}

// World-space NW corner (max X north, max Y west) of MCNK (chunkCol, chunkRow)
// inside ADT tile (blockX = north-south tile index, blockY = west-east).
//   row (north-south, 0 = north) advances southward  -> decreasing X
//   col (west-east,   0 = west)  advances eastward    -> decreasing Y
// NOTE: the mapping of MCNK header IndexX/IndexY onto (row,col) is the one fact
// that must be confirmed against a real tile (same class of risk as the WDT
// x/y order). See terrain.cpp.
inline Vec3 chunkCornerWorld(int blockX, int blockY, int row, int col, float baseHeight) {
    double x = (32.0 - blockX) * TILE_SIZE - row * CHUNK_SIZE; // north
    double y = (32.0 - blockY) * TILE_SIZE - col * CHUNK_SIZE; // west
    return { static_cast<float>(x), static_cast<float>(y), baseHeight };
}

} // namespace wf
